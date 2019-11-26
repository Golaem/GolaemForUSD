/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#include "glmUSDDataImpl.h"

USD_INCLUDES_START
#include <pxr/pxr.h>
#include <pxr/usd/sdf/schema.h>
USD_INCLUDES_END

#include <glmCore.h>
#include <glmLog.h>
#include <glmFileDir.h>
#include <glmSimulationCacheLibrary.h>
#include <glmSimulationCacheInformation.h>

#include <fstream>

namespace glm
{
    namespace usdplugin
    {
        // All leaf prims have the same properties, so we set up some static data about
        // these properties that will always be true.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
        // Define tokens for the property names we know about from usdGeom
        TF_DEFINE_PRIVATE_TOKENS(
            _entityPropertyTokens,
            ((xformOpOrder, "xformOpOrder"))((xformOpTranslate, "xformOp:translate"))((xformOpRotateXYZ, "xformOp:rotateXYZ"))((displayColor, "primvars:displayColor")));

#ifdef _MSC_VER
#pragma warning(pop)
#endif
        // We create a static map from property names to the info about them that
        // we'll be querying for specs.
        struct _LeafPrimPropertyInfo
        {
            VtValue defaultValue;
            TfToken typeName;
            // Most of our properties are aniated.
            bool isAnimated{true};
        };

        using _LeafPrimPropertyMap =
            std::map<TfToken, _LeafPrimPropertyInfo, TfTokenFastArbitraryLessThan>;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4459)
#endif

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _entityProperties)
        {

            // Define the default value types for our animated properties.
            (*_entityProperties)[_entityPropertyTokens->xformOpTranslate].defaultValue =
                VtValue(GfVec3d(0));
            (*_entityProperties)[_entityPropertyTokens->xformOpRotateXYZ].defaultValue =
                VtValue(GfVec3f(0));
            (*_entityProperties)[_entityPropertyTokens->displayColor].defaultValue =
                VtValue(VtVec3fArray({GfVec3f(1)}));

            // xformOpOrder is a non-animated property and is specifically translate,
            // rotate for all our geom prims.
            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].defaultValue =
                VtValue(VtTokenArray{_entityPropertyTokens->xformOpTranslate,
                                     _entityPropertyTokens->xformOpRotateXYZ});
            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_entityProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

#ifdef _MSC_VER
#pragma warning(pop)
#endif

        // Helper function for getting the root prim path.
        static const SdfPath& _GetRootPrimPath()
        {
            static const SdfPath rootPrimPath("/Root");
            return rootPrimPath;
        }

// Helper macro for many of our functions need to optionally set an output
// VtValue when returning true.
#define RETURN_TRUE_WITH_OPTIONAL_VALUE(val) \
    if (value)                               \
    {                                        \
        *value = VtValue(val);               \
    }                                        \
    return true;

        //-----------------------------------------------------------------------------
        GolaemUSD_DataImpl::GolaemUSD_DataImpl(const GolaemUSD_DataParams& params)
            : _params(params)
        {
            usdplugin::init();
            _InitFromParams();
        }

        //-----------------------------------------------------------------------------
        GolaemUSD_DataImpl::~GolaemUSD_DataImpl()
        {
            usdplugin::finish();
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::IsEmpty() const
        {
            return _primSpecPaths.empty();
        }

        //-----------------------------------------------------------------------------
        SdfSpecType GolaemUSD_DataImpl::GetSpecType(const SdfPath& path) const
        {
            // All specs are generated.
            if (path.IsPropertyPath())
            {
                // A specific set of defined properties exist on the leaf prims only
                // as attributes. Non leaf prims have no properties.
                if (_entityProperties->count(path.GetNameToken()) &&
                    _entityDataMap.count(path.GetAbsoluteRootOrPrimPath()))
                {
                    return SdfSpecTypeAttribute;
                }
            }
            else
            {
                // Special case for pseudoroot.
                if (path == SdfPath::AbsoluteRootPath())
                {
                    return SdfSpecTypePseudoRoot;
                }
                // All other valid prim spec paths are cached.
                if (_primSpecPaths.count(path))
                {
                    return SdfSpecTypePrim;
                }
            }

            return SdfSpecTypeUnknown;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::Has(const SdfPath& path, const TfToken& field, VtValue* value) const
        {
            // If property spec, check property fields
            if (path.IsPropertyPath())
            {

                if (field == SdfFieldKeys->TypeName)
                {
                    return _HasPropertyTypeNameValue(path, value);
                }
                else if (field == SdfFieldKeys->Default)
                {
                    return _HasPropertyDefaultValue(path, value);
                }
                else if (field == SdfFieldKeys->TimeSamples)
                {
                    // Only animated properties have time samples.
                    if (_IsAnimatedProperty(path))
                    {
                        // Will need to generate the full SdfTimeSampleMap with a
                        // time sample value for each discrete animated frame if the
                        // value of the TimeSamples field is requested. Use a generator
                        // function in case we don't need to output the value as this
                        // can be expensive.
                        auto _MakeTimeSampleMap = [this, &path]() {
                            SdfTimeSampleMap sampleMap;
                            for (auto time : _animTimeSampleTimes)
                            {
                                QueryTimeSample(path, time, &sampleMap[time]);
                            }
                            return sampleMap;
                        };

                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_MakeTimeSampleMap());
                    }
                }
            }
            else if (path == SdfPath::AbsoluteRootPath())
            {
                // Special case check for the pseudoroot prim spec.
                if (field == SdfChildrenKeys->PrimChildren)
                {
                    // Pseudoroot only has the root prim as a child
                    static TfTokenVector rootChildren(
                        {_GetRootPrimPath().GetNameToken()});
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(rootChildren);
                }
                // Default prim is always the root prim.
                if (field == SdfFieldKeys->DefaultPrim)
                {
                    if (path == SdfPath::AbsoluteRootPath())
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_GetRootPrimPath().GetNameToken());
                    }
                }
                if (field == SdfFieldKeys->StartTimeCode)
                {
                    if (path == SdfPath::AbsoluteRootPath())
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(double(_startFrame));
                    }
                }
                if (field == SdfFieldKeys->EndTimeCode)
                {
                    if (path == SdfPath::AbsoluteRootPath())
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(double(_endFrame));
                    }
                }
            }
            else
            {
                // Otherwise check prim spec fields.
                if (field == SdfFieldKeys->Specifier)
                {
                    // All our prim specs use the "def" specifier.
                    if (_primSpecPaths.count(path))
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(SdfSpecifierDef);
                    }
                }

                if (field == SdfFieldKeys->TypeName)
                {
                    // Only the leaf prim specs have a type name determined from the
                    // params.
                    if (_entityDataMap.count(path))
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE("Mesh");
                    }
                }

                if (field == SdfChildrenKeys->PrimChildren)
                {
                    // Non-leaf prims have the prim children. The list is the same set
                    // of prim child names for each non-leaf prim regardless of depth.
                    if (_primSpecPaths.count(path) && !_entityDataMap.count(path))
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_primChildNames);
                    }
                }

                if (field == SdfChildrenKeys->PropertyChildren)
                {
                    // Leaf prims have the same specified set of property children.
                    if (_entityDataMap.count(path))
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_entityPropertyTokens->allTokens);
                    }
                }
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::VisitSpecs(const SdfAbstractData& data, SdfAbstractDataSpecVisitor* visitor) const
        {
            // Visit the pseudoroot.
            if (!visitor->VisitSpec(data, SdfPath::AbsoluteRootPath()))
            {
                return;
            }
            // Visit all the cached prim spec paths.
            for (const auto& path : _primSpecPaths)
            {
                if (!visitor->VisitSpec(data, path))
                {
                    return;
                }
            }
            // Visit the property specs which exist only on leaf prims.
            for (auto it : _entityDataMap)
            {
                for (const TfToken& propertyName : _entityPropertyTokens->allTokens)
                {
                    if (!visitor->VisitSpec(data, it.first.AppendProperty(propertyName)))
                    {
                        return;
                    }
                }
            }
        }

        //-----------------------------------------------------------------------------
        const std::vector<TfToken>& GolaemUSD_DataImpl::List(const SdfPath& path) const
        {
            if (path.IsPropertyPath())
            {
                // For properties, check that it's a valid leaf prim property
                const _LeafPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
                if (propInfo && _entityDataMap.count(path.GetAbsoluteRootOrPrimPath()))
                {
                    // Include time sample field in the property is animated.
                    if (propInfo->isAnimated)
                    {
                        static std::vector<TfToken> animPropFields(
                            {SdfFieldKeys->TypeName,
                             SdfFieldKeys->Default,
                             SdfFieldKeys->TimeSamples});
                        return animPropFields;
                    }
                    else
                    {
                        static std::vector<TfToken> nonAnimPropFields(
                            {SdfFieldKeys->TypeName,
                             SdfFieldKeys->Default});
                        return nonAnimPropFields;
                    }
                }
            }
            else if (path == SdfPath::AbsoluteRootPath())
            {
                // Pseudoroot fields.
                static std::vector<TfToken> pseudoRootFields(
                    {SdfChildrenKeys->PrimChildren,
                     SdfFieldKeys->DefaultPrim,
                     SdfFieldKeys->StartTimeCode,
                     SdfFieldKeys->EndTimeCode});
                return pseudoRootFields;
            }
            else if (_primSpecPaths.count(path))
            {
                // Prim spec. Different fields for leaf and non-leaf prims.
                if (_entityDataMap.count(path))
                {
                    static std::vector<TfToken> leafPrimFields(
                        {SdfFieldKeys->Specifier,
                         SdfFieldKeys->TypeName,
                         SdfChildrenKeys->PropertyChildren});
                    return leafPrimFields;
                }
                else
                {
                    static std::vector<TfToken> nonLeafPrimFields(
                        {SdfFieldKeys->Specifier,
                         SdfChildrenKeys->PrimChildren});
                    return nonLeafPrimFields;
                }
            }

            static std::vector<TfToken> empty;
            return empty;
        }

        //-----------------------------------------------------------------------------
        const std::set<double>& GolaemUSD_DataImpl::ListAllTimeSamples() const
        {
            // The set of all time sample times is cached.
            return _animTimeSampleTimes;
        }

        //-----------------------------------------------------------------------------
        const std::set<double>& GolaemUSD_DataImpl::ListTimeSamplesForPath(const SdfPath& path) const
        {
            // All animated properties use the same set of time samples; all other
            // specs return empty.
            if (_IsAnimatedProperty(path))
            {
                return ListAllTimeSamples();
            }
            static std::set<double> empty;
            return empty;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::GetBracketingTimeSamples(double time, double* tLower, double* tUpper) const
        {
            // A time sample time will exist at each discrete integer frame for the
            // duration of the generated animation and will already be cached.
            if (_animTimeSampleTimes.empty())
            {
                return false;
            }

            // First time sample is always _startFrame.
            if (time <= _startFrame)
            {
                *tLower = *tUpper = _startFrame;
                return true;
            }
            // Last time sample will alway be size - 1.
            if (time >= _endFrame)
            {
                *tLower = *tUpper = _endFrame;
                return true;
            }
            // Lower bound is the integer time. Upper bound will be the same unless the
            // time itself is non-integer, in which case it'll be the next integer time.
            *tLower = *tUpper = int(time);
            if (time > *tUpper)
            {
                *tUpper += 1.0;
            }
            return true;
        }

        //-----------------------------------------------------------------------------
        size_t GolaemUSD_DataImpl::GetNumTimeSamplesForPath(const SdfPath& path) const
        {
            // All animated properties use the same set of time samples; all other specs
            // have no time samples.
            if (_IsAnimatedProperty(path))
            {
                return _animTimeSampleTimes.size();
            }
            return 0;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::GetBracketingTimeSamplesForPath(const SdfPath& path, double time, double* tLower, double* tUpper) const
        {
            // All animated properties use the same set of time samples.
            if (_IsAnimatedProperty(path))
            {
                return GetBracketingTimeSamples(time, tLower, tUpper);
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::QueryTimeSample(const SdfPath& path, double time, VtValue* value) const
        {
            GLM_UNREFERENCED(time);
            // Only leaf prim properties have time samples
            const EntityData* val = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
            if (!val)
            {
                return false;
            }

            if (path.GetNameToken() == _entityPropertyTokens->xformOpTranslate)
            {
                // Animated position, anchored at the prim's layout position.
                RETURN_TRUE_WITH_OPTIONAL_VALUE(val->pos);
            }
            if (path.GetNameToken() == _entityPropertyTokens->xformOpRotateXYZ)
            {
                // Animated rotation.
                RETURN_TRUE_WITH_OPTIONAL_VALUE(GfVec3f());
            }
            if (path.GetNameToken() == _entityPropertyTokens->displayColor)
            {
                // Animated color value.
                RETURN_TRUE_WITH_OPTIONAL_VALUE(VtVec3fArray({GfVec3f(1, 0.5, 0)}));
            }
            return false;
        }

        //-----------------------------------------------------------------------------
        void loadSimulationCacheLib(glm::crowdio::SimulationCacheLibrary& simuCacheLibrary, const glm::GlmString& cacheLibPath)
        {
            if (!cacheLibPath.empty() && glm::FileDir::exist(cacheLibPath.c_str()))
            {
                std::ifstream inFile(cacheLibPath.c_str());
                if (inFile.is_open())
                {
                    inFile.seekg(0, std::ios::end);
                    size_t fileSize = inFile.tellg();
                    inFile.seekg(0);

                    std::string fileContents(fileSize + 1, '\0');
                    inFile.read(&fileContents[0], fileSize);
                    simuCacheLibrary.loadLibrary(fileContents.c_str(), fileContents.size(), false);
                }
                else
                {
                    GLM_CROWD_TRACE_ERROR("Failed to open Golaem simulation cache library file '" << cacheLibPath << "'");
                }
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_InitFromParams()
        {
            _startFrame = 0;
            _endFrame = -1;
            if (_params.glmCacheLibFile.IsEmpty() || _params.glmCacheLibItem.IsEmpty())
            {
                return;
            }

            GLM_CROWD_TRACE_ERROR("Test Message");

            glm::crowdio::SimulationCacheLibrary simuCacheLibrary;
            loadSimulationCacheLib(simuCacheLibrary, _params.glmCacheLibFile.data());

            glm::GlmString cfNames;
            glm::GlmString cacheName;
            glm::GlmString cacheDir;
            glm::GlmString characterFiles;
            glm::GlmString srcTerrainFile;
            glm::GlmString dstTerrainFile;
            bool enableLayout = false;
            glm::GlmString layoutFiles;

            glm::crowdio::SimulationCacheInformation* cacheInfo = simuCacheLibrary.getCacheInformationByItemName(_params.glmCacheLibItem.data());
            if (cacheInfo == NULL && simuCacheLibrary.getCacheInformationCount() > 0)
            {
                GLM_CROWD_TRACE_WARNING("Could not find simulation cache item '"
                                        << _params.glmCacheLibItem.data() << "' in library file '" << _params.glmCacheLibFile.data() << "'");
                cacheInfo = &simuCacheLibrary.getCacheInformation(0);
            }
            if (cacheInfo != NULL)
            {
                cfNames = cacheInfo->_crowdFields;
                cacheName = cacheInfo->_cacheName;
                cacheDir = cacheInfo->_cacheDir;
                characterFiles = cacheInfo->_characterFiles;
                srcTerrainFile = cacheInfo->_srcTerrain;
                dstTerrainFile = cacheInfo->_destTerrain;
                enableLayout = cacheInfo->_enableLayout;
                layoutFiles = cacheInfo->_layoutFile;
                layoutFiles.trim(";");
            }

            _factory.loadGolaemCharacters(characterFiles.c_str());

            glm::Array<glm::GlmString> layoutFilesArray = glm::stringToStringArray(layoutFiles, ";");
            size_t layoutCount = layoutFilesArray.size();
            if (enableLayout && layoutCount > 0)
            {
                for (size_t iLayout = 0; iLayout < layoutCount; ++iLayout)
                {
                    const glm::GlmString& layoutFile = layoutFilesArray[iLayout];
                    if (layoutFile.length() > 0)
                    {
                        _factory.loadLayoutHistoryFile(_factory.getLayoutHistoryCount(), layoutFile.c_str());
                    }
                }
            }

            glm::crowdio::crowdTerrain::TerrainMesh* sourceTerrain = NULL;
            glm::crowdio::crowdTerrain::TerrainMesh* destTerrain = NULL;
            if (!srcTerrainFile.empty())
            {
                sourceTerrain = glm::crowdio::crowdTerrain::loadTerrainAsset(srcTerrainFile.c_str());
            }
            if (!dstTerrainFile.empty())
            {
                destTerrain = glm::crowdio::crowdTerrain::loadTerrainAsset(dstTerrainFile.c_str());
            }
            if (destTerrain == NULL)
            {
                destTerrain = sourceTerrain;
            }
            _factory.setTerrainMeshes(sourceTerrain, destTerrain);

            bool framesFound = false;
            glm::Array<glm::GlmString> crowdFieldNames = glm::stringToStringArray(cfNames.c_str(), ";");
            for (size_t iCf = 0, cfCount = crowdFieldNames.size(); iCf < cfCount; ++iCf)
            {
                const glm::GlmString& cfName = crowdFieldNames[iCf];
                if (cfName.empty())
                {
                    continue;
                }
                glm::crowdio::CachedSimulation& cachedSimulation = _factory.getCachedSimulation(cacheDir.c_str(), cacheName.c_str(), cfName.c_str());
                const glm::crowdio::GlmSimulationData* simuData = cachedSimulation.getFinalSimulationData();
                //const glm::crowdio::GlmFrameData* frameData = cachedSimulation.getFinalFrameData(currentFrame, UINT32_MAX, true);

                if (simuData == NULL /*|| frameData == NULL*/)
                {
                    continue;
                }

                if (!framesFound)
                {
                    framesFound = cachedSimulation.getSrcFrameRangeAvailableOnDisk(_startFrame, _endFrame);
                }
                else
                {
                    int cfStartFrame = 0;
                    int cfEndFrame = 0;
                    framesFound = cachedSimulation.getSrcFrameRangeAvailableOnDisk(cfStartFrame, cfEndFrame);
                    if (framesFound)
                    {
                        _startFrame = glm::max(cfStartFrame, _startFrame);
                        _endFrame = glm::min(cfEndFrame, _endFrame);
                    }
                }
            }

            for (size_t iFrame = _startFrame; iFrame <= _endFrame; ++iFrame)
            {
                _animTimeSampleTimes.insert(double(iFrame));
            }
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_IsAnimatedProperty(const SdfPath& path) const
        {
            // Check that it is a property id.
            if (!path.IsPropertyPath())
            {
                return false;
            }
            // Check that its one of our animated property names.
            const _LeafPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (!(propInfo && propInfo->isAnimated))
            {
                return false;
            }
            // Check that it belongs to a leaf prim.
            return TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_HasPropertyDefaultValue(const SdfPath& path, VtValue* value) const
        {
            // Check that it is a property id.
            if (!path.IsPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const _LeafPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (!propInfo)
            {
                return false;
            }

            // Check that it belongs to a leaf prim before getting the default value
            const EntityData* val = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
            if (val)
            {
                if (value)
                {
                    // Special case for translate property. Each leaf prim has its own
                    // default position.
                    if (path.GetNameToken() == _entityPropertyTokens->xformOpTranslate)
                    {
                        *value = VtValue(val->pos);
                    }
                    else
                    {
                        *value = propInfo->defaultValue;
                    }
                }
                return true;
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_HasPropertyTypeNameValue(const SdfPath& path, VtValue* value) const
        {
            // Check that it is a property id.
            if (!path.IsPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const _LeafPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (!propInfo)
            {
                return false;
            }

            // Check that it belongs to a leaf prim before getting the type name value
            const EntityData* val = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
            if (val)
            {
                if (value)
                {
                    *value = VtValue(propInfo->typeName);
                }
                return true;
            }

            return false;
        }

    } // namespace usdplugin
} // namespace glm