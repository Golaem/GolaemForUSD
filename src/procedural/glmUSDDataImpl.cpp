/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#include "glmUSDDataImpl.h"

USD_INCLUDES_START
#include <pxr/pxr.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
USD_INCLUDES_END

#include <glmCore.h>
#include <glmLog.h>
#include <glmFileDir.h>
#include <glmSimulationCacheLibrary.h>
#include <glmSimulationCacheInformation.h>
#include <glmGolaemCharacter.h>
#include <glmCrowdFBXStorage.h>
#include <glmCrowdFBXBaker.h>
#include <glmCrowdFBXCharacter.h>
#include <glmCrowdGcgCharacter.h>

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
        // clang-format off
        // Define tokens for the property names we know about from usdGeom

        TF_DEFINE_PRIVATE_TOKENS(
            _entityPropertyTokens,
            ((xformOpOrder, "xformOpOrder"))
            ((xformOpTranslate, "xformOp:translate"))
            ((displayColor, "primvars:displayColor"))
            ((visibility, "visibility"))
            ((entityId, "entityId")));

        TF_DEFINE_PRIVATE_TOKENS(
            _entityMeshPropertyTokens,
            ((faceVertexCounts, "faceVertexCounts"))
            ((faceVertexIndices, "faceVertexIndices"))
            ((orientation, "orientation"))
            ((points, "points"))
            ((subdivisionScheme, "subdivisionScheme"))
            ((normals, "normals")));
        // clang-format on
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        // We create a static map from property names to the info about them that
        // we'll be querying for specs.
        struct _EntityPrimPropertyInfo
        {
            VtValue defaultValue;
            TfToken typeName;
            // Most of our properties are aniated.
            bool isAnimated = true;
            bool hasInterpolation = false;
            TfToken interpolation;
        };

        using _LeafPrimPropertyMap =
            std::map<TfToken, _EntityPrimPropertyInfo, TfTokenFastArbitraryLessThan>;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4459)
#endif

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _entityProperties)
        {

            // Define the default value types for our animated properties.
            (*_entityProperties)[_entityPropertyTokens->xformOpTranslate].defaultValue =
                VtValue(GfVec3f(0));

            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].defaultValue = VtValue(VtTokenArray{_entityPropertyTokens->xformOpTranslate});
            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].isAnimated = false;

            (*_entityProperties)[_entityPropertyTokens->displayColor].defaultValue = VtValue(VtVec3fArray({GfVec3f(1, 0.5, 0)}));
            (*_entityProperties)[_entityPropertyTokens->displayColor].isAnimated = false;

            (*_entityProperties)[_entityPropertyTokens->visibility].defaultValue = VtValue(UsdGeomTokens->inherited);

            (*_entityProperties)[_entityPropertyTokens->entityId].defaultValue = VtValue(int64_t(-1));
            (*_entityProperties)[_entityPropertyTokens->entityId].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_entityProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _entityMeshProperties)
        {

            // Define the default value types for our animated properties.
            (*_entityMeshProperties)[_entityMeshPropertyTokens->points].defaultValue = VtValue(VtVec3fArray());

            (*_entityMeshProperties)[_entityMeshPropertyTokens->normals].defaultValue = VtValue(VtVec3fArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->normals].hasInterpolation = true;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->normals].interpolation = UsdGeomTokens->faceVarying;

            // set the subdivision scheme to none in order to take normals into account
            (*_entityMeshProperties)[_entityMeshPropertyTokens->subdivisionScheme].defaultValue = UsdGeomTokens->none;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->subdivisionScheme].isAnimated = false;

            // keep faceVertexCounts and faceVertexIndices animated, as when an entity is invalid (killed), they will be empty
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexCounts].defaultValue = VtValue(VtIntArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexIndices].defaultValue = VtValue(VtIntArray());

            (*_entityMeshProperties)[_entityMeshPropertyTokens->orientation].defaultValue = VtValue(TfToken("rightHanded"));
            (*_entityMeshProperties)[_entityMeshPropertyTokens->orientation].isAnimated = false;
            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_entityMeshProperties)
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

        static glm::Mutex _fbxMutex;

        //-----------------------------------------------------------------------------
        glm::crowdio::CrowdFBXStorage& getFbxStorage()
        {
            glm::ScopedLock<glm::Mutex> lock(_fbxMutex);
            static glm::crowdio::CrowdFBXStorage fbxStorage;
            return fbxStorage;
        }

        //-----------------------------------------------------------------------------
        glm::crowdio::CrowdFBXBaker& getFbxBaker()
        {
            glm::crowdio::CrowdFBXStorage& fbxStorage = getFbxStorage();
            glm::ScopedLock<glm::Mutex> lock(_fbxMutex);
            static glm::crowdio::CrowdFBXBaker fbxBaker(fbxStorage.touchFbxSdkManager());
            return fbxBaker;
        }

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
                    TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL)
                {
                    return SdfSpecTypeAttribute;
                }

                if (_entityMeshProperties->count(path.GetNameToken()) &&
                    TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL)
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
                else if (field == UsdGeomTokens->interpolation)
                {
                    return _HasPropertyInterpolation(path, value);
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
                    if (TfMapLookupPtr(_entityDataMap, path) != NULL)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken("Xform"));
                    }

                    if (TfMapLookupPtr(_entityMeshDataMap, path) != NULL)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken("Mesh"));
                    }
                }

                if (field == SdfFieldKeys->Active)
                {
                    // Only leaf prim properties have time samples
                    const EntityData* entityData = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
                    if (entityData != NULL)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(!entityData->data.excluded);
                    }
                }

                if (field == SdfChildrenKeys->PrimChildren)
                {
                    // Non-leaf prims have the prim children. The list is the same set
                    // of prim child names for each non-leaf prim regardless of depth.
                    if (_primSpecPaths.count(path) && TfMapLookupPtr(_entityMeshDataMap, path) == NULL)
                    {
                        const std::vector<TfToken>* childNames = TfMapLookupPtr(_primChildNames, path);
                        if (childNames != NULL)
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(*childNames);
                        }
                    }
                }

                if (field == SdfChildrenKeys->PropertyChildren)
                {
                    // Leaf prims have the same specified set of property children.
                    if (TfMapLookupPtr(_entityDataMap, path) != NULL)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_entityPropertyTokens->allTokens);
                    }
                    if (TfMapLookupPtr(_entityMeshDataMap, path) != NULL)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(_entityMeshPropertyTokens->allTokens);
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
            // Visit the property specs which exist only on entity prims.
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
            // Visit the property specs which exist only on entity mesh prims.
            for (auto it : _entityMeshDataMap)
            {
                for (const TfToken& propertyName : _entityMeshPropertyTokens->allTokens)
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
                static std::vector<TfToken> animPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default,
                     SdfFieldKeys->TimeSamples});
                static std::vector<TfToken> nonAnimPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default});
                {
                    const _EntityPrimPropertyInfo* entityPropInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
                    if (entityPropInfo && TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL)
                    {
                        // Include time sample field in the property is animated.
                        if (entityPropInfo->isAnimated)
                        {
                            return animPropFields;
                        }
                        else
                        {

                            return nonAnimPropFields;
                        }
                    }
                }
                {
                    const _EntityPrimPropertyInfo* entityMeshPropInfo = TfMapLookupPtr(*_entityMeshProperties, path.GetNameToken());
                    if (entityMeshPropInfo && TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL)
                    {
                        // Include time sample field in the property is animated.
                        if (entityMeshPropInfo->isAnimated)
                        {
                            if (entityMeshPropInfo->hasInterpolation)
                            {
                                static std::vector<TfToken> animInterpPropFields(
                                    {SdfFieldKeys->TypeName,
                                     SdfFieldKeys->Default,
                                     SdfFieldKeys->TimeSamples,
                                     UsdGeomTokens->interpolation});
                                return animInterpPropFields;
                            }
                            return animPropFields;
                        }
                        else
                        {
                            if (entityMeshPropInfo->hasInterpolation)
                            {
                                static std::vector<TfToken> nonAnimInterpPropFields(
                                    {SdfFieldKeys->TypeName,
                                     SdfFieldKeys->Default,
                                     UsdGeomTokens->interpolation});
                                return nonAnimInterpPropFields;
                            }
                            return nonAnimPropFields;
                        }
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
                if (TfMapLookupPtr(_entityDataMap, path) != NULL)
                {
                    static std::vector<TfToken> entityPrimFields(
                        {SdfFieldKeys->Specifier,
                         SdfFieldKeys->TypeName,
                         SdfFieldKeys->Active,
                         SdfChildrenKeys->PrimChildren,
                         SdfChildrenKeys->PropertyChildren});
                    return entityPrimFields;
                }
                else if (TfMapLookupPtr(_entityMeshDataMap, path) != NULL)
                {
                    static std::vector<TfToken> entityMeshPrimFields(
                        {SdfFieldKeys->Specifier,
                         SdfFieldKeys->TypeName,
                         SdfChildrenKeys->PropertyChildren});
                    return entityMeshPrimFields;
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
            // Last time sample will always be _endFrame.
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
            // Only leaf prim properties have time samples
            const EntityData* entityData = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
            const EntityMeshData* meshData = NULL;
            if (entityData == NULL)
            {
                meshData = TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath());
                entityData = meshData->entityData;
            }
            if (entityData == NULL || entityData->data.excluded)
            {
                return false;
            }

            _ComputeEntity(entityData, time);

            if (meshData == NULL)
            {
                // this is an entity node
                if (path.GetNameToken() == _entityPropertyTokens->xformOpTranslate)
                {
                    // Animated position, anchored at the prim's layout position.
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.pos);
                }
                if (path.GetNameToken() == _entityPropertyTokens->visibility)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.enabled ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);
                }
            }
            else
            {
                // this is a mesh node
                if (path.GetNameToken() == _entityMeshPropertyTokens->points)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.points);
                }
                if (path.GetNameToken() == _entityMeshPropertyTokens->normals)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.normals);
                }
                if (path.GetNameToken() == _entityMeshPropertyTokens->faceVertexCounts)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.faceVertexCounts);
                }
                if (path.GetNameToken() == _entityMeshPropertyTokens->faceVertexIndices)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.faceVertexIndices);
                }
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
        glm::GlmString sanitizeMeshName(const glm::GlmString& meshName)
        {
            return glm::replaceString(meshName, ":", "_");
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

            glm::crowdio::SimulationCacheLibrary simuCacheLibrary;
            loadSimulationCacheLib(simuCacheLibrary, _params.glmCacheLibFile.GetText());

            glm::GlmString cfNames;
            glm::GlmString cacheName;
            glm::GlmString cacheDir;
            glm::GlmString characterFiles;
            glm::GlmString srcTerrainFile;
            glm::GlmString dstTerrainFile;
            bool enableLayout = false;
            glm::GlmString layoutFiles;

            glm::crowdio::SimulationCacheInformation* cacheInfo = simuCacheLibrary.getCacheInformationByItemName(_params.glmCacheLibItem.GetText());
            if (cacheInfo == NULL && simuCacheLibrary.getCacheInformationCount() > 0)
            {
                GLM_CROWD_TRACE_WARNING("Could not find simulation cache item '"
                                        << _params.glmCacheLibItem.GetText() << "' in library file '" << _params.glmCacheLibFile.GetText() << "'");
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
            // override cacheInfo params if neeeded
            if (!_params.glmCrowdFieldNames.IsEmpty())
            {
                cfNames = _params.glmCrowdFieldNames.GetText();
            }
            if (!_params.glmCacheName.IsEmpty())
            {
                cacheName = _params.glmCacheName.GetText();
            }
            if (!_params.glmCacheDir.IsEmpty())
            {
                cacheDir = _params.glmCacheDir.GetText();
            }
            if (!_params.glmCharacterFiles.IsEmpty())
            {
                characterFiles = _params.glmCharacterFiles.GetText();
            }
            if (!_params.glmSourceTerrain.IsEmpty())
            {
                srcTerrainFile = _params.glmSourceTerrain.GetText();
            }
            if (!_params.glmDestTerrain.IsEmpty())
            {
                dstTerrainFile = _params.glmDestTerrain.GetText();
            }
            enableLayout = enableLayout && _params.glmEnableLayout;
            if (!_params.glmLayoutFiles.IsEmpty())
            {
                layoutFiles = _params.glmLayoutFiles.GetText();
            }

            float renderPercent = _params.glmDrawPercent * 0.01f;
            short geoTag = _params.glmGeoTag;

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

            // Layer always has a root spec that is the default prim of the layer.
            _primSpecPaths.insert(_GetRootPrimPath());
            std::vector<TfToken>& rootChildNames = _primChildNames[_GetRootPrimPath()];

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

                if (simuData == NULL)
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

            glm::Array<glm::GlmString> entityMeshNames;
            for (size_t iCf = 0, cfCount = crowdFieldNames.size(); iCf < cfCount; ++iCf)
            {
                const glm::GlmString& cfName = crowdFieldNames[iCf];
                if (cfName.empty())
                {
                    continue;
                }

                SdfPath cfPath = _GetRootPrimPath().AppendChild(TfToken(cfName.c_str()));
                _primSpecPaths.insert(cfPath);
                rootChildNames.push_back(TfToken(cfName.c_str()));

                std::vector<TfToken>& cfChildNames = _primChildNames[cfPath];

                glm::crowdio::CachedSimulation& cachedSimulation = _factory.getCachedSimulation(cacheDir.c_str(), cacheName.c_str(), cfName.c_str());

                const glm::crowdio::GlmSimulationData* simuData = cachedSimulation.getFinalSimulationData();

                if (simuData == NULL)
                {
                    continue;
                }

                // create lock for cached simulation
                glm::SpinLock& cachedSimulationLock = _cachedSimulationLocks[&cachedSimulation];

                glm::PODArray<int64_t> excludedEntities;
                glm::Array<const glm::crowdio::glmHistoryRuntimeStructure*> historyStructures;
                cachedSimulation.getHistoryRuntimeStructures(historyStructures);
                glm::crowdio::createEntityExclusionList(excludedEntities, cachedSimulation.getSrcSimulationData(), _factory.getLayoutHistories(), historyStructures);
                size_t maxEntities = (size_t)floorf(simuData->_entityCount * renderPercent);
                for (uint32_t iEntity = 0; iEntity < simuData->_entityCount; ++iEntity)
                {

                    int64_t entityId = simuData->_entityIds[iEntity];
                    if (entityId < 0)
                    {
                        // entity was probably killed
                        continue;
                    }

                    glm::GlmString entityName = "Entity_" + glm::toString(entityId);

                    SdfPath entityPath = cfPath.AppendChild(TfToken(entityName.c_str()));
                    _primSpecPaths.insert(entityPath);
                    cfChildNames.push_back(TfToken(entityName.c_str()));

                    std::vector<TfToken>& entityChildNames = _primChildNames[entityPath];

                    EntityData& entityData = _entityDataMap[entityPath];
                    entityData.data.computedTimeSample = _startFrame - 1; // ensure there will be a compute in QueryTimeSample
                    //entityData.data.inputGeoData._dirMapRules             // left empty for now
                    entityData.data.inputGeoData._entityId = entityId;
                    entityData.data.inputGeoData._entityIndex = iEntity;
                    entityData.data.inputGeoData._cachedSimulation = &cachedSimulation;
                    entityData.data.inputGeoData._geometryTag = geoTag;
                    entityData.data.inputGeoData._fbxStorage = &getFbxStorage();
                    entityData.data.inputGeoData._fbxBaker = &getFbxBaker();
                    entityData.data.cachedSimulationLock = &cachedSimulationLock;

                    bool excludedEntity = iEntity >= maxEntities;
                    if (!excludedEntity)
                    {
                        size_t excludedEntityIdx;
                        excludedEntity = glm::glmFindIndex(excludedEntities.begin(), excludedEntities.end(), entityId, excludedEntityIdx);
                    }

                    entityData.data.excluded = excludedEntity;

                    if (excludedEntity)
                    {
                        continue;
                    }

                    int32_t characterIdx = simuData->_characterIdx[iEntity];
                    const glm::GolaemCharacter* character = _factory.getGolaemCharacter(characterIdx);
                    if (character == NULL)
                    {
                        GLM_CROWD_TRACE_ERROR_LIMIT("The entity '" << entityId << "' has an invalid character index: '" << characterIdx << "'. Skipping it. Please assign a Rendering Type from the Rendering Attributes panel");
                        entityData.data.excluded = true;
                        continue;
                    }
                    entityData.character = character;
                    int32_t renderingTypeIdx = simuData->_renderingTypeIdx[iEntity];
                    const glm::RenderingType* renderingType = NULL;
                    if (renderingTypeIdx >= 0 && renderingTypeIdx < character->_renderingTypes.sizeInt())
                    {
                        renderingType = &character->_renderingTypes[renderingTypeIdx];
                    }

                    if (renderingType == NULL)
                    {
                        GLM_CROWD_TRACE_WARNING_LIMIT("The entity '" << entityId << "', character '" << character->_name << "' has an invalid rendering type: '" << renderingTypeIdx << "'. Using default rendering type.");
                    }

                    _ComputeEntityMeshNames(entityMeshNames, &entityData);

                    for (size_t iMesh = 0, meshCount = entityMeshNames.size(); iMesh < meshCount; ++iMesh)
                    {
                        TfToken meshName(sanitizeMeshName(entityMeshNames[iMesh]).c_str());
                        SdfPath meshPath = entityPath.AppendChild(meshName);
                        _primSpecPaths.insert(meshPath);
                        entityChildNames.push_back(meshName);
                        EntityMeshData& meshData = _entityMeshDataMap[meshPath];
                        meshData.entityData = &entityData;
                        entityData.meshIds[meshPath] = iMesh;
                        entityData.data.meshData.push_back(&meshData);
                    }
                }
            }

            for (int iFrame = _startFrame; iFrame <= _endFrame; ++iFrame)
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
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                return propInfo->isAnimated && TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL;
            }
            propInfo = TfMapLookupPtr(*_entityMeshProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                return propInfo->isAnimated && TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath()) != NULL;
            }
            return false;
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
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
                if (entityData)
                {
                    if (value)
                    {
                        // Special case for translate property. Each leaf prim has its own
                        // default position.
                        if (path.GetNameToken() == _entityPropertyTokens->xformOpTranslate)
                        {
                            *value = VtValue(entityData->data.pos);
                        }
                        else if (path.GetNameToken() == _entityPropertyTokens->visibility)
                        {
                            *value = VtValue(entityData->data.enabled ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);
                        }
                        else if (path.GetNameToken() == _entityPropertyTokens->entityId)
                        {
                            *value = VtValue(entityData->data.inputGeoData._entityId);
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
            propInfo = TfMapLookupPtr(*_entityMeshProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const EntityMeshData* entityMeshData = TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath());
                if (entityMeshData)
                {
                    if (value)
                    {
                        if (path.GetNameToken() == _entityMeshPropertyTokens->points)
                        {
                            *value = VtValue(entityMeshData->data.points);
                        }
                        else if (path.GetNameToken() == _entityMeshPropertyTokens->normals)
                        {
                            *value = VtValue(entityMeshData->data.normals);
                        }
                        else if (path.GetNameToken() == _entityMeshPropertyTokens->faceVertexCounts)
                        {
                            *value = VtValue(entityMeshData->data.faceVertexCounts);
                        }
                        else if (path.GetNameToken() == _entityMeshPropertyTokens->faceVertexIndices)
                        {
                            *value = VtValue(entityMeshData->data.faceVertexIndices);
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

            return false;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_HasPropertyInterpolation(const SdfPath& path, VtValue* value) const
        {
            // Check that it is a property id.
            if (!path.IsPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityMeshProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the interpolation value
                const EntityMeshData* entityMeshData = TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath());
                if (entityMeshData)
                {
                    if (value)
                    {
                        if (propInfo->hasInterpolation)
                        {
                            *value = VtValue(propInfo->interpolation);
                        }
                    }
                    return propInfo->hasInterpolation;
                }
                return false;
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
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
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
            propInfo = TfMapLookupPtr(*_entityMeshProperties, path.GetNameToken());
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the type name value
                const EntityMeshData* val = TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath());
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
            return false;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeEntityMeshNames(glm::Array<glm::GlmString>& meshNames, const EntityData* entityData) const
        {
            meshNames.clear();
            GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;
            switch (displayMode)
            {
            case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
            {
                meshNames.push_back("BBOX");
            }
            break;
            case glm::usdplugin::GolaemDisplayMode::SKELETON:
            {
            }
            break;
            case glm::usdplugin::GolaemDisplayMode::SKINMESH:
            {
                glm::crowdio::OutputEntityGeoData outputData; // TODO: see if storage is better
                glm::crowdio::GlmGeometryGenerationStatus geoStatus = glm::crowdio::GIO_SUCCESS;
                // this is run without any frame, it will only compute mesh names
                geoStatus = glm::crowdio::glmPrepareEntityGeometry(&entityData->data.inputGeoData, &outputData);
                if (geoStatus == glm::crowdio::GIO_SUCCESS)
                {
                    size_t meshCount = outputData._meshAssetNameIndices.size();
                    meshNames.resize(meshCount);
                    for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                    {
                        glm::GlmString& meshName = meshNames[iRenderMesh];
                        meshName = outputData._meshAssetNames[outputData._meshAssetNameIndices[iRenderMesh]];
                        int materialIdx = outputData._meshAssetMaterialIndices[iRenderMesh];
                        if (materialIdx != 0)
                        {
                            meshName += glm::toString(materialIdx);
                        }
                    }
                }
            }
            break;
            default:
                break;
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeEntity(const EntityData* entityData, double time) const
        {
            // check if computation is needed
            glm::ScopedLock<glm::SpinLock> entityComputeLock(entityData->data._entityComputeLock);
            if (entityData->data.computedTimeSample != time)
            {
                entityData->data.computedTimeSample = time;

                const glm::crowdio::GlmSimulationData* simuData = NULL;
                const glm::crowdio::GlmFrameData* frameData = NULL;
                {
                    glm::ScopedLock<glm::SpinLock> cachedSimuLock(*entityData->data.cachedSimulationLock);
                    simuData = entityData->data.inputGeoData._cachedSimulation->getFinalSimulationData();
                    frameData = entityData->data.inputGeoData._cachedSimulation->getFinalFrameData(time, UINT32_MAX, true);
                }
                if (simuData == NULL || frameData == NULL)
                {
                    _InvalidateEntity(entityData);
                }
                entityData->data.enabled = frameData->_entityEnabled[entityData->data.inputGeoData._entityIndex] == 1;
                if (!entityData->data.enabled)
                {
                    _InvalidateEntity(entityData);
                }

                if (entityData->data.enabled)
                {
                    GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;

                    glm::crowdio::OutputEntityGeoData outputData; // TODO: see if storage is better
                    glm::crowdio::GlmGeometryGenerationStatus geoStatus = glm::crowdio::GIO_SUCCESS;

                    if (displayMode == GolaemDisplayMode::SKINMESH)
                    {
                        // update frame before computing geometry
                        entityData->data.inputGeoData._frames.resize(1);
                        entityData->data.inputGeoData._frames[0] = time;
                        entityData->data.inputGeoData._frameDatas.resize(1);
                        entityData->data.inputGeoData._frameDatas[0] = frameData;
                    }

                    if (entityData->data.firstCompute)
                    {
                        uint16_t entityType = simuData->_entityTypes[entityData->data.inputGeoData._entityIndex];

                        uint16_t boneCount = simuData->_boneCount[entityType];
                        entityData->data.bonePositionOffset = simuData->_iBoneOffsetPerEntityType[entityType] + simuData->_indexInEntityType[entityData->data.inputGeoData._entityIndex] * boneCount;

                        switch (displayMode)
                        {
                        case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                        {
                            // compute the bounding box of the current entity
                            glm::Vector3 halfExtents(1, 1, 1);
                            const glm::GeometryAsset* geoAsset = entityData->character->getGeometryAsset(entityData->data.inputGeoData._geometryTag, 0); // any LOD should have same extents !
                            if (geoAsset != NULL)
                            {
                                halfExtents = geoAsset->_halfExtentsYUp;
                            }
                            float characterScale = simuData->_scales[entityData->data.inputGeoData._entityIndex];
                            halfExtents *= characterScale;

                            // create the shape of the bounding box
                            const EntityMeshData* bboxMeshData = entityData->data.meshData[0];
                            VtVec3fArray& points = bboxMeshData->data.points;
                            points.resize(8);

                            points[0].Set(
                                -halfExtents[0],
                                -halfExtents[1],
                                +halfExtents[2]);

                            points[1].Set(
                                +halfExtents[0],
                                -halfExtents[1],
                                +halfExtents[2]);

                            points[2].Set(
                                +halfExtents[0],
                                -halfExtents[1],
                                -halfExtents[2]);

                            points[3].Set(
                                -halfExtents[0],
                                -halfExtents[1],
                                -halfExtents[2]);

                            points[4].Set(
                                -halfExtents[0],
                                +halfExtents[1],
                                +halfExtents[2]);

                            points[5].Set(
                                +halfExtents[0],
                                +halfExtents[1],
                                +halfExtents[2]);

                            points[6].Set(
                                +halfExtents[0],
                                +halfExtents[1],
                                -halfExtents[2]);

                            points[7].Set(
                                -halfExtents[0],
                                +halfExtents[1],
                                -halfExtents[2]);

                            bboxMeshData->data.faceVertexCounts.resize(6);
                            for (size_t iFace = 0; iFace < 6; ++iFace)
                            {
                                bboxMeshData->data.faceVertexCounts[iFace] = 4;
                            }

                            // face 0
                            bboxMeshData->data.faceVertexIndices.push_back(0);
                            bboxMeshData->data.faceVertexIndices.push_back(1);
                            bboxMeshData->data.faceVertexIndices.push_back(2);
                            bboxMeshData->data.faceVertexIndices.push_back(3);

                            // face 1
                            bboxMeshData->data.faceVertexIndices.push_back(1);
                            bboxMeshData->data.faceVertexIndices.push_back(5);
                            bboxMeshData->data.faceVertexIndices.push_back(6);
                            bboxMeshData->data.faceVertexIndices.push_back(2);

                            // face 2
                            bboxMeshData->data.faceVertexIndices.push_back(2);
                            bboxMeshData->data.faceVertexIndices.push_back(6);
                            bboxMeshData->data.faceVertexIndices.push_back(7);
                            bboxMeshData->data.faceVertexIndices.push_back(3);

                            // face 3
                            bboxMeshData->data.faceVertexIndices.push_back(3);
                            bboxMeshData->data.faceVertexIndices.push_back(7);
                            bboxMeshData->data.faceVertexIndices.push_back(4);
                            bboxMeshData->data.faceVertexIndices.push_back(0);

                            // face 4
                            bboxMeshData->data.faceVertexIndices.push_back(0);
                            bboxMeshData->data.faceVertexIndices.push_back(4);
                            bboxMeshData->data.faceVertexIndices.push_back(5);
                            bboxMeshData->data.faceVertexIndices.push_back(1);

                            // face 5
                            bboxMeshData->data.faceVertexIndices.push_back(4);
                            bboxMeshData->data.faceVertexIndices.push_back(7);
                            bboxMeshData->data.faceVertexIndices.push_back(6);
                            bboxMeshData->data.faceVertexIndices.push_back(5);

                            VtVec3fArray& vertexNormals = bboxMeshData->data.normals;
                            vertexNormals.resize(24);

                            int vertexIdx = 0;

                            // face 0
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(0, -1, 0);
                            }

                            // face 1
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(1, 0, 0);
                            }

                            // face 2
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(0, 0, -1);
                            }

                            // face 3
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(-1, 0, 0);
                            }

                            // face 4
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(0, 0, 1);
                            }

                            // face 5
                            for (int iVtx = 0; iVtx < 4; ++iVtx, ++vertexIdx)
                            {
                                vertexNormals[vertexIdx].Set(0, 1, 0);
                            }
                        }
                        break;
                        case glm::usdplugin::GolaemDisplayMode::SKELETON:
                        {
                        }
                        break;
                        case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                        {
                            geoStatus = glm::crowdio::glmPrepareEntityGeometry(&entityData->data.inputGeoData, &outputData);
                            if (geoStatus == glm::crowdio::GIO_SUCCESS)
                            {
                                size_t meshCount = outputData._meshAssetNameIndices.size();
                                glm::PODArray<int> meshShadingGroups(meshCount, -1);
                                glm::Array<glm::Array<glm::Vector3>>& frameDeformedVertices = outputData._deformedVertices[0];

                                if (outputData._geoType == glm::crowdio::GeometryType::FBX)
                                {
                                    glm::Array<glm::PODArray<int>> fbxVertexMasks(meshCount);
                                    glm::Array<glm::PODArray<int>> fbxPolygonMasks(meshCount);
                                    for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                                    {
                                        size_t iGeoFileMesh = outputData._meshAssetNameIndices[iRenderMesh];

                                        // meshDeformedVertices contains all fbx points, not just the ones that were filtered by vertexMasks
                                        const glm::Array<glm::Vector3>& meshDeformedVertices = frameDeformedVertices[iGeoFileMesh];
                                        size_t vertexCount = meshDeformedVertices.size();
                                        if (vertexCount == 0)
                                        {
                                            continue;
                                        }

                                        const EntityMeshData* meshData = entityData->data.meshData[iRenderMesh];

                                        // when fbxMesh == NULL, vertexCount == 0, so no need to check fbxMesh != NULL
                                        FbxMesh* fbxMesh = outputData._fbxCharacter->getCharacterFBXMesh(iGeoFileMesh);

                                        FbxLayer* fbxLayer0 = fbxMesh->GetLayer(0);
                                        bool hasMaterials = false;
                                        FbxLayerElementMaterial* materialElement = NULL;
                                        if (fbxLayer0 != NULL)
                                        {
                                            materialElement = fbxLayer0->GetMaterials();
                                            hasMaterials = materialElement != NULL;
                                        }

                                        unsigned int fbxVertexCount = fbxMesh->GetControlPointsCount();
                                        unsigned int fbxPolyCount = fbxMesh->GetPolygonCount();
                                        glm::PODArray<int>& vertexMasks = fbxVertexMasks[iRenderMesh];
                                        glm::PODArray<int>& polygonMasks = fbxPolygonMasks[iRenderMesh];

                                        vertexMasks.assign(fbxVertexCount, -1);
                                        polygonMasks.assign(fbxPolyCount, 0);

                                        int meshMtlIdx = outputData._meshAssetMaterialIndices[iRenderMesh];

                                        // check material id and reconstruct data
                                        for (unsigned int iFbxPoly = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                        {
                                            int currentMtlIdx = 0;
                                            if (hasMaterials)
                                            {
                                                currentMtlIdx = materialElement->GetIndexArray().GetAt(iFbxPoly);
                                            }
                                            if (currentMtlIdx == meshMtlIdx)
                                            {
                                                polygonMasks[iFbxPoly] = 1;
                                                int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                                for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex)
                                                {
                                                    int iFbxVertex = fbxMesh->GetPolygonVertex(iFbxPoly, iPolyVertex);
                                                    int& vertexMask = vertexMasks[iFbxVertex];
                                                    if (vertexMask >= 0)
                                                    {
                                                        continue;
                                                    }
                                                    vertexMask = 0;
                                                }
                                            }
                                        }

                                        unsigned int iActualVertex = 0;
                                        for (unsigned int iFbxVertex = 0; iFbxVertex < fbxVertexCount; ++iFbxVertex)
                                        {
                                            int& vertexMask = vertexMasks[iFbxVertex];
                                            if (vertexMask >= 0)
                                            {
                                                vertexMask = iActualVertex;
                                                ++iActualVertex;
                                            }
                                        }

                                        meshData->data.points.resize(iActualVertex);

                                        const glm::GlmString& meshName = outputData._meshAssetNames[iGeoFileMesh];
                                        int& shadingGroupIdx = meshShadingGroups[iRenderMesh];

                                        // find shader assets
                                        int iMaterial = outputData._meshAssetMaterialIndices[iRenderMesh];
                                        int meshAssetIdx = entityData->character->findMeshAssetIdx(meshName);
                                        if (meshAssetIdx != -1)
                                        {
                                            const glm::MeshAsset& meshAsset = entityData->character->_meshAssets[meshAssetIdx];
                                            if (iMaterial < meshAsset._shadingGroups.sizeInt())
                                            {
                                                shadingGroupIdx = meshAsset._shadingGroups[iMaterial];
                                            }
                                        }

                                        if (shadingGroupIdx == -1)
                                        {
                                            GLM_CROWD_TRACE_WARNING_LIMIT("No Shading Group found for mesh " << meshName << ". Using default material shader instead");
                                        }

                                        int polyVertexCount = 0;
                                        for (unsigned int iFbxPoly = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                        {
                                            if (polygonMasks[iFbxPoly])
                                            {
                                                int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                                meshData->data.faceVertexCounts.push_back(polySize);
                                                polyVertexCount += polySize;
                                                for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex)
                                                {
                                                    // do not reverse polygon order
                                                    int iFbxVertex = fbxMesh->GetPolygonVertex(iFbxPoly, iPolyVertex);
                                                    int vertexId = vertexMasks[iFbxVertex];
                                                    meshData->data.faceVertexIndices.push_back(vertexId);
                                                } // iPolyVertex
                                            }
                                        }
                                        meshData->data.normals.resize((size_t)polyVertexCount);
                                    }
                                }
                                else if (outputData._geoType == glm::crowdio::GeometryType::GCG)
                                {
                                    for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                                    {
                                        size_t iGeoFileMesh = outputData._meshAssetNameIndices[iRenderMesh];

                                        const glm::Array<glm::Vector3>& meshDeformedVertices = frameDeformedVertices[iRenderMesh];
                                        size_t vertexCount = meshDeformedVertices.size();
                                        if (vertexCount == 0)
                                        {
                                            continue;
                                        }

                                        const EntityMeshData* meshData = entityData->data.meshData[iRenderMesh];
                                        meshData->data.points.resize(vertexCount);

                                        glm::crowdio::GlmFileMeshTransform& assetFileMeshTransform = outputData._gcgCharacter->getGeometry()._transforms[outputData._transformIndicesInGcgFile[iRenderMesh]];
                                        glm::crowdio::GlmFileMesh& assetFileMesh = outputData._gcgCharacter->getGeometry()._meshes[assetFileMeshTransform._meshIndex];

                                        const glm::GlmString& meshName = outputData._meshAssetNames[iGeoFileMesh];
                                        int& shadingGroupIdx = meshShadingGroups[iRenderMesh];

                                        // find shader assets
                                        int iMaterial = outputData._meshAssetMaterialIndices[iRenderMesh];
                                        int meshAssetIdx = entityData->character->findMeshAssetIdx(meshName);
                                        if (meshAssetIdx != -1)
                                        {
                                            const glm::MeshAsset& meshAsset = entityData->character->_meshAssets[meshAssetIdx];
                                            if (iMaterial < meshAsset._shadingGroups.sizeInt())
                                            {
                                                shadingGroupIdx = meshAsset._shadingGroups[iMaterial];
                                            }
                                        }

                                        if (shadingGroupIdx == -1)
                                        {
                                            GLM_CROWD_TRACE_WARNING_LIMIT("No Shading Group found for mesh " << meshName << ". Using default material shader instead");
                                        }

                                        for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                        {
                                            uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                            meshData->data.faceVertexCounts.push_back(polySize);
                                            for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                            {
                                                // do not reverse polygon order
                                                int vertexId = assetFileMesh._polygonsVertexIndices[iVertex];
                                                meshData->data.faceVertexIndices.push_back(vertexId);
                                            }
                                        }
                                        meshData->data.normals.resize(assetFileMesh._polygonsTotalVertexCount);
                                    }
                                }
                            }
                        }
                        break;
                        default:
                            break;
                        }
                    }

                    // update entity position

                    float* rootPos = frameData->_bonePositions[entityData->data.bonePositionOffset];
                    entityData->data.pos.Set(rootPos);

                    switch (displayMode)
                    {
                    case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                    {
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKELETON:
                    {
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                    {
                        if (!entityData->data.firstCompute)
                        {
                            geoStatus = glm::crowdio::glmPrepareEntityGeometry(&entityData->data.inputGeoData, &outputData);
                        }
                        if (geoStatus == glm::crowdio::GIO_SUCCESS)
                        {
                            size_t meshCount = outputData._meshAssetNameIndices.size();

                            glm::Array<glm::Array<glm::Vector3>>& frameDeformedVertices = outputData._deformedVertices[0];
                            glm::Array<glm::Array<glm::Vector3>>& frameDeformedNormals = outputData._deformedNormals[0];

                            if (outputData._geoType == glm::crowdio::GeometryType::FBX)
                            {
                                // ----- FBX specific data
                                FbxAMatrix nodeTransform;
                                FbxAMatrix geomTransform;
                                FbxAMatrix identityMatrix;
                                identityMatrix.SetIdentity();
                                FbxTime fbxTime;
                                FbxVector4 fbxVect;
                                // ----- end FBX specific data

                                // Extract frame
                                if (outputData._geoBeInfo._idGeometryFileIdx != -1)
                                {
                                    float(&geometryFrameCacheData)[3] = frameData->_geoBehaviorAnimFrameInfo[outputData._geoBeInfo._geoDataIndex];
                                    double frameRate(FbxTime::GetFrameRate(outputData._fbxCharacter->touchFBXScene()->GetGlobalSettings().GetTimeMode()));
                                    fbxTime.SetGlobalTimeMode(FbxTime::eCustom, frameRate);
                                    fbxTime.SetMilliSeconds(long((double)geometryFrameCacheData[0] / frameRate * 1000.0));
                                }
                                else
                                {
                                    fbxTime = 0;
                                }

                                for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                                {
                                    size_t iGeoFileMesh = outputData._meshAssetNameIndices[iRenderMesh];

                                    // meshDeformedVertices contains all fbx points, not just the ones that were filtered by vertexMasks
                                    const glm::Array<glm::Vector3>& meshDeformedVertices = frameDeformedVertices[iGeoFileMesh];
                                    size_t vertexCount = meshDeformedVertices.size();
                                    if (vertexCount == 0)
                                    {
                                        continue;
                                    }

                                    const EntityMeshData* meshData = entityData->data.meshData[iRenderMesh];

                                    // when fbxMesh == NULL, vertexCount == 0, so no need to check fbxMesh != NULL
                                    FbxNode* fbxNode = outputData._fbxCharacter->getCharacterFBXMeshes()[iGeoFileMesh];
                                    FbxMesh* fbxMesh = outputData._fbxCharacter->getCharacterFBXMesh(iGeoFileMesh);

                                    // for each mesh, get the transform in case of its position in not relative to the center of the world
                                    outputData._fbxCharacter->getMeshGlobalTransform(nodeTransform, fbxNode, fbxTime);
                                    glm::crowdio::CrowdFBXBaker::getGeomTransform(geomTransform, fbxNode);
                                    nodeTransform *= geomTransform;

                                    FbxLayer* fbxLayer0 = fbxMesh->GetLayer(0);
                                    bool hasNormals = false;
                                    bool hasMaterials = false;
                                    FbxLayerElementMaterial* materialElement = NULL;
                                    if (fbxLayer0 != NULL)
                                    {
                                        hasNormals = fbxLayer0->GetNormals() != NULL;
                                        materialElement = fbxLayer0->GetMaterials();
                                        hasMaterials = materialElement != NULL;
                                    }

                                    bool hasTransform = !(nodeTransform == identityMatrix);

                                    unsigned int fbxVertexCount = fbxMesh->GetControlPointsCount();
                                    unsigned int fbxPolyCount = fbxMesh->GetPolygonCount();

                                    glm::PODArray<int> vertexMasks;
                                    glm::PODArray<int> polygonMasks;

                                    vertexMasks.assign(fbxVertexCount, -1);
                                    polygonMasks.assign(fbxPolyCount, 0);

                                    int meshMtlIdx = outputData._meshAssetMaterialIndices[iRenderMesh];

                                    // check material id and reconstruct data
                                    for (unsigned int iFbxPoly = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                    {
                                        int currentMtlIdx = 0;
                                        if (hasMaterials)
                                        {
                                            currentMtlIdx = materialElement->GetIndexArray().GetAt(iFbxPoly);
                                        }
                                        if (currentMtlIdx == meshMtlIdx)
                                        {
                                            polygonMasks[iFbxPoly] = 1;
                                            for (int iPolyVertex = 0, polyVertexCount = fbxMesh->GetPolygonSize(iFbxPoly); iPolyVertex < polyVertexCount; ++iPolyVertex)
                                            {
                                                int iFbxVertex = fbxMesh->GetPolygonVertex(iFbxPoly, iPolyVertex);
                                                int& vertexMask = vertexMasks[iFbxVertex];
                                                if (vertexMask >= 0)
                                                {
                                                    continue;
                                                }
                                                vertexMask = 0;
                                            }
                                        }
                                    }

                                    for (unsigned int iFbxVertex = 0, iActualVertex = 0; iFbxVertex < fbxVertexCount; ++iFbxVertex)
                                    {
                                        int& vertexMask = vertexMasks[iFbxVertex];
                                        if (vertexMask >= 0)
                                        {
                                            vertexMask = iActualVertex;
                                            ++iActualVertex;
                                        }
                                    }

                                    unsigned int iActualVertex = 0;
                                    for (unsigned int iFbxVertex = 0; iFbxVertex < fbxVertexCount; ++iFbxVertex)
                                    {
                                        int& vertexMask = vertexMasks[iFbxVertex];
                                        if (vertexMask >= 0)
                                        {
                                            // meshDeformedVertices contains all fbx points, not just the ones that were filtered by vertexMasks
                                            GfVec3f& point = meshData->data.points[iActualVertex];
                                            // vertices
                                            if (hasTransform)
                                            {
                                                const Vector3& glmVect = meshDeformedVertices[iFbxVertex];
                                                fbxVect.Set(glmVect.x, glmVect.y, glmVect.z);
                                                // transform vertex in case of local transformation
                                                fbxVect = nodeTransform.MultT(fbxVect);
                                                point.Set((float)fbxVect[0], (float)fbxVect[1], (float)fbxVect[2]);
                                            }
                                            else
                                            {
                                                const Vector3& meshVertex = meshDeformedVertices[iFbxVertex];
                                                point.Set(meshVertex.getFloatValues());
                                            }

                                            point -= entityData->data.pos;

                                            ++iActualVertex;
                                        }
                                    }

                                    if (hasNormals)
                                    {
                                        FbxAMatrix globalRotate(identityMatrix);
                                        globalRotate.SetR(nodeTransform.GetR());
                                        bool hasRotate = globalRotate != identityMatrix;

                                        const glm::Array<glm::Vector3>& meshDeformedNormals = frameDeformedNormals[iGeoFileMesh];

                                        // normals are always stored per polygon vertex
                                        for (unsigned int iFbxPoly = 0, iFbxNormal = 0, iActualPolyVertex = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                        {
                                            int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                            if (polygonMasks[iFbxPoly])
                                            {
                                                for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex, ++iFbxNormal, ++iActualPolyVertex)
                                                {
                                                    // meshDeformedNormals contains all fbx normals, not just the ones that were filtered by polygonMasks
                                                    // do not reverse polygon order
                                                    if (hasRotate)
                                                    {
                                                        const Vector3& glmVect = meshDeformedNormals[iFbxNormal];
                                                        fbxVect.Set(glmVect.x, glmVect.y, glmVect.z);
                                                        fbxVect = globalRotate.MultT(fbxVect);
                                                        meshData->data.normals[iActualPolyVertex].Set(
                                                            (float)fbxVect[0], (float)fbxVect[1], (float)fbxVect[2]);
                                                    }
                                                    else
                                                    {
                                                        const glm::Vector3& deformedNormal = meshDeformedNormals[iFbxNormal];
                                                        meshData->data.normals[iActualPolyVertex].Set(
                                                            deformedNormal.getFloatValues());
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                iFbxNormal += polySize;
                                            }
                                        }
                                    }
                                }
                            }
                            else if (outputData._geoType == glm::crowdio::GeometryType::GCG)
                            {

                                for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                                {
                                    const glm::Array<glm::Vector3>& meshDeformedVertices = frameDeformedVertices[iRenderMesh];
                                    size_t vertexCount = meshDeformedVertices.size();
                                    if (vertexCount == 0)
                                    {
                                        continue;
                                    }

                                    const EntityMeshData* meshData = entityData->data.meshData[iRenderMesh];

                                    for (size_t iVertex = 0; iVertex < vertexCount; ++iVertex)
                                    {
                                        const glm::Vector3& meshVertex = meshDeformedVertices[iVertex];
                                        GfVec3f& point = meshData->data.points[iVertex];
                                        point.Set(meshVertex.getFloatValues());
                                        point -= entityData->data.pos;
                                    }

                                    const glm::Array<glm::Vector3>& meshDeformedNormals = frameDeformedNormals[iRenderMesh];

                                    glm::crowdio::GlmFileMeshTransform& assetFileMeshTransform = outputData._gcgCharacter->getGeometry()._transforms[outputData._transformIndicesInGcgFile[iRenderMesh]];
                                    glm::crowdio::GlmFileMesh& assetFileMesh = outputData._gcgCharacter->getGeometry()._meshes[assetFileMeshTransform._meshIndex];

                                    // add normals
                                    if (assetFileMesh._normalMode == glm::crowdio::GLM_NORMAL_PER_POLYGON_VERTEX)
                                    {
                                        for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                        {
                                            uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                            for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                            {
                                                // do not reverse polygon order
                                                const glm::Vector3& vtxNormal = meshDeformedNormals[iVertex];
                                                meshData->data.normals[iVertex].Set(vtxNormal.getFloatValues());
                                            }
                                        }
                                    }
                                    else
                                    {
                                        uint32_t* polygonNormalIndices = assetFileMesh._normalMode == glm::crowdio::GLM_NORMAL_PER_CONTROL_POINT ? assetFileMesh._polygonsVertexIndices : assetFileMesh._polygonsNormalIndices;
                                        for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                        {
                                            uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                            for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                            {
                                                // do not reverse polygon order
                                                uint32_t normalIdx = polygonNormalIndices[iVertex];
                                                const glm::Vector3& vtxNormal = meshDeformedNormals[normalIdx];
                                                meshData->data.normals[iVertex].Set(vtxNormal.getFloatValues());
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                    default:
                        break;
                    }
                    entityData->data.firstCompute = false;
                }
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_InvalidateEntity(const EntityData* entityData) const
        {
            entityData->data.enabled = false;
            entityData->data.firstCompute = true;
            entityData->data.inputGeoData._frames.clear();
            entityData->data.inputGeoData._frameDatas.clear();
            for (size_t iMesh = 0, meshCount = entityData->data.meshData.size(); iMesh < meshCount; ++iMesh)
            {
                const EntityMeshData* meshData = entityData->data.meshData[iMesh];
                meshData->data.points.clear();
                meshData->data.normals.clear();
                meshData->data.faceVertexCounts.clear();
                meshData->data.faceVertexIndices.clear();
            }
        }

    } // namespace usdplugin
} // namespace glm