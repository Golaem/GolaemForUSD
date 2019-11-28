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
#include <glmGolaemCharacter.h>

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
            ((entityId, "entityId")));

        TF_DEFINE_PRIVATE_TOKENS(
            _entityMeshPropertyTokens,
            ((faceVertexCounts, "faceVertexCounts"))
            ((faceVertexIndices, "faceVertexIndices"))
            ((orientation, "orientation"))
            ((points, "points")));
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
            bool isAnimated{true};
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

            (*_entityProperties)[_entityPropertyTokens->displayColor].defaultValue =
                VtValue(VtVec3fArray({GfVec3f(1, 0.5, 0)}));

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

            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexCounts].defaultValue = VtValue(VtIntArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexCounts].isAnimated = false;

            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexIndices].defaultValue = VtValue(VtIntArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexIndices].isAnimated = false;

            (*_entityMeshProperties)[_entityMeshPropertyTokens->orientation].defaultValue = VtValue(TfToken("leftHanded"));
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
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(!entityData->data.excluded && entityData->data.enabled);
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
                            return animPropFields;
                        }
                        else
                        {
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
            // Only leaf prim properties have time samples
            const EntityData* entityData = TfMapLookupPtr(_entityDataMap, path.GetAbsoluteRootOrPrimPath());
            const EntityMeshData* meshData = NULL;
            if (entityData == NULL)
            {
                meshData = TfMapLookupPtr(_entityMeshDataMap, path.GetAbsoluteRootOrPrimPath());
                entityData = meshData->entityData;
            }
            if (entityData == NULL)
            {
                return false;
            }

            // check if computation is needed
            if (entityData->data.computedTimeSample != time)
            {
                entityData->data.computedTimeSample = time;
                if (entityData->data.excluded)
                {
                    return false;
                }

                const glm::crowdio::GlmSimulationData* simuData = NULL;
                const glm::crowdio::GlmFrameData* frameData = NULL;
                {
                    glm::ScopedLock<glm::SpinLock> lock(*entityData->data.cachedSimulationLock);
                    simuData = entityData->data.inputGeoData._cachedSimulation->getFinalSimulationData();
                    frameData = entityData->data.inputGeoData._cachedSimulation->getFinalFrameData(time, UINT32_MAX, true);
                }
                if (simuData == NULL || frameData == NULL)
                {
                    entityData->data.enabled = false;
                    entityData->data.firstCompute = true;
                    return false;
                }

                entityData->data.enabled = frameData->_entityEnabled[entityData->data.inputGeoData._entityIndex] == 1;
                if (!entityData->data.enabled)
                {
                    entityData->data.firstCompute = true;
                    return false;
                }

                GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;

                if (entityData->data.firstCompute)
                {
                    entityData->data.firstCompute = false;
                    switch (displayMode)
                    {
                    case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                    {
                        // compute the bounding box of the current entity
                        glm::Vector3 halfExtents(1, 1, 1);
                        const glm::GeometryAsset* geoAsset = entityData->data.character->getGeometryAsset(entityData->data.inputGeoData._geometryTag, 0); // any LOD should have same extents !
                        if (geoAsset != NULL)
                        {
                            halfExtents = geoAsset->_halfExtentsYUp;
                        }
                        float characterScale = simuData->_scales[entityData->data.inputGeoData._entityIndex];
                        halfExtents *= characterScale;
                        // create the shape of the bounding box
                        const EntityMeshData* bboxMeshData = entityData->data.meshData[0];
                        bboxMeshData->data.points.resize(8);

                        bboxMeshData->data.points[0].Set(
                            -halfExtents[0],
                            -halfExtents[1],
                            +halfExtents[2]);

                        bboxMeshData->data.points[1].Set(
                            +halfExtents[0],
                            -halfExtents[1],
                            +halfExtents[2]);

                        bboxMeshData->data.points[2].Set(
                            +halfExtents[0],
                            -halfExtents[1],
                            -halfExtents[2]);

                        bboxMeshData->data.points[3].Set(
                            -halfExtents[0],
                            -halfExtents[1],
                            -halfExtents[2]);

                        bboxMeshData->data.points[4].Set(
                            -halfExtents[0],
                            +halfExtents[1],
                            +halfExtents[2]);

                        bboxMeshData->data.points[5].Set(
                            +halfExtents[0],
                            +halfExtents[1],
                            +halfExtents[2]);

                        bboxMeshData->data.points[6].Set(
                            +halfExtents[0],
                            +halfExtents[1],
                            -halfExtents[2]);

                        bboxMeshData->data.points[7].Set(
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
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKELETON:
                    {
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                    {
                    }
                    break;
                    default:
                        break;
                    }
                }

                switch (displayMode)
                {
                case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                {
                    uint16_t entityType = simuData->_entityTypes[entityData->data.inputGeoData._entityIndex];

                    uint16_t boneCount = simuData->_boneCount[entityType];
                    uint32_t positionOffset = simuData->_iBoneOffsetPerEntityType[entityType] + simuData->_indexInEntityType[entityData->data.inputGeoData._entityIndex] * boneCount;

                    float* rootPos = frameData->_bonePositions[positionOffset];
                    entityData->data.pos.Set(rootPos);
                }
                break;
                case glm::usdplugin::GolaemDisplayMode::SKELETON:
                {
                }
                break;
                case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                {
                }
                break;
                default:
                    break;
                }
            }

            if (meshData == NULL)
            {
                // this is an entity node
                if (path.GetNameToken() == _entityPropertyTokens->xformOpTranslate)
                {
                    // Animated position, anchored at the prim's layout position.
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.pos);
                }
                if (path.GetNameToken() == _entityPropertyTokens->displayColor)
                {
                    // Animated color value.
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(VtVec3fArray({GfVec3f(1, 0.5, 0)}));
                }
            }
            else
            {
                // this is a mesh node
                if (path.GetNameToken() == _entityMeshPropertyTokens->points)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.points);
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
        void GolaemUSD_DataImpl::_InitFromParams()
        {
            _startFrame = 0;
            _endFrame = -1;
            if (_params.glmCacheLibFile.IsEmpty() || _params.glmCacheLibItem.IsEmpty())
            {
                return;
            }

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
            // override cacheInfo params if neeeded
            if (!_params.glmCrowdFieldNames.IsEmpty())
            {
                cfNames = _params.glmCrowdFieldNames.data();
            }
            if (!_params.glmCacheName.IsEmpty())
            {
                cacheName = _params.glmCacheName.data();
            }
            if (!_params.glmCacheDir.IsEmpty())
            {
                cacheDir = _params.glmCacheDir.data();
            }
            if (!_params.glmCharacterFiles.IsEmpty())
            {
                characterFiles = _params.glmCharacterFiles.data();
            }
            if (!_params.glmSourceTerrain.IsEmpty())
            {
                srcTerrainFile = _params.glmSourceTerrain.data();
            }
            if (!_params.glmDestTerrain.IsEmpty())
            {
                dstTerrainFile = _params.glmDestTerrain.data();
            }
            enableLayout = enableLayout && _params.glmEnableLayout;
            if (!_params.glmLayoutFiles.IsEmpty())
            {
                layoutFiles = _params.glmLayoutFiles.data();
            }

            float renderPercent = _params.glmDrawPercent * 0.01f;
            short geoTag = _params.glmGeoTag;
            GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;

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

                SdfPath cfPath = _GetRootPrimPath().AppendChild(TfToken(cfName.c_str()));
                _primSpecPaths.insert(cfPath);
                rootChildNames.push_back(TfToken(cfName.c_str()));

                std::vector<TfToken>& cfChildNames = _primChildNames[cfPath];

                glm::crowdio::CachedSimulation& cachedSimulation = _factory.getCachedSimulation(cacheDir.c_str(), cacheName.c_str(), cfName.c_str());

                // create lock for cached simulation
                glm::SpinLock& cachedSimulationLock = _cachedSimulationLocks[&cachedSimulation];

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
                    entityData.data.inputGeoData._entityId = entityId;
                    entityData.data.inputGeoData._entityIndex = iEntity;
                    entityData.data.inputGeoData._cachedSimulation = &cachedSimulation;
                    entityData.data.inputGeoData._geometryTag = geoTag;
                    entityData.data.cachedSimulationLock = &cachedSimulationLock;

                    bool excludedEntity = false;
                    if (!excludedEntity)
                    {
                        excludedEntity = iEntity >= maxEntities;
                        if (!excludedEntity)
                        {
                            size_t excludedEntityIdx;
                            excludedEntity = glm::glmFindIndex(excludedEntities.begin(), excludedEntities.end(), entityId, excludedEntityIdx);
                        }
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
                    entityData.data.character = character;
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

                    switch (displayMode)
                    {
                    case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                    {
                        static TfToken meshName = TfToken("BBOX");
                        SdfPath meshPath = entityPath.AppendChild(meshName);
                        _primSpecPaths.insert(meshPath);
                        entityChildNames.push_back(meshName);
                        EntityMeshData& meshData = _entityMeshDataMap[meshPath];
                        meshData.entityData = &entityData;
                        meshData.meshIdx = 0;
                        entityData.data.meshData.push_back(&meshData);
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKELETON:
                    {
                    }
                    break;
                    case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                    {
                    }
                    break;
                    default:
                        break;
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

    } // namespace usdplugin
} // namespace glm