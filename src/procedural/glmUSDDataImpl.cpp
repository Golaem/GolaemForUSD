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
            _geomCommonTokens,      
            (inherited)             
            (faceVarying)           
            (rightHanded)           
            (none)                   
            (interpolation)        
            (invisible));

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
            ((normals, "normals"))
            ((uvs, "primvars:st")));

        TF_DEFINE_PRIVATE_TOKENS(
            _entityMeshRelationshipTokens,
            ((materialBinding, "material:binding")));
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

        struct _EntityPrimRelationshipInfo
        {
            SdfPathListOp defaultTargetPath;
        };

        using _LeafPrimRelationshiphMap =
            std::map<TfToken, _EntityPrimRelationshipInfo, TfTokenFastArbitraryLessThan>;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4459)
#endif

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _entityProperties)
        {

            // Define the default value types for our animated properties.
            (*_entityProperties)[_entityPropertyTokens->xformOpTranslate].defaultValue = VtValue(GfVec3f(0));

            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].defaultValue = VtValue(VtTokenArray{_entityPropertyTokens->xformOpTranslate});
            (*_entityProperties)[_entityPropertyTokens->xformOpOrder].isAnimated = false;

            (*_entityProperties)[_entityPropertyTokens->displayColor].defaultValue = VtValue(VtVec3fArray({GfVec3f(1, 0.5, 0)}));
            (*_entityProperties)[_entityPropertyTokens->displayColor].isAnimated = false;

            (*_entityProperties)[_entityPropertyTokens->visibility].defaultValue = VtValue(_geomCommonTokens->inherited);

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
            (*_entityMeshProperties)[_entityMeshPropertyTokens->normals].interpolation = _geomCommonTokens->faceVarying;

            // set the subdivision scheme to none in order to take normals into account
            (*_entityMeshProperties)[_entityMeshPropertyTokens->subdivisionScheme].defaultValue = _geomCommonTokens->none;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->subdivisionScheme].isAnimated = false;

            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexCounts].defaultValue = VtValue(VtIntArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexCounts].isAnimated = false;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexIndices].defaultValue = VtValue(VtIntArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->faceVertexIndices].isAnimated = false;

            (*_entityMeshProperties)[_entityMeshPropertyTokens->uvs].defaultValue = VtValue(VtVec2fArray());
            (*_entityMeshProperties)[_entityMeshPropertyTokens->uvs].isAnimated = false;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->uvs].hasInterpolation = true;
            (*_entityMeshProperties)[_entityMeshPropertyTokens->uvs].interpolation = _geomCommonTokens->faceVarying;

            (*_entityMeshProperties)[_entityMeshPropertyTokens->orientation].defaultValue = VtValue(_geomCommonTokens->rightHanded);
            (*_entityMeshProperties)[_entityMeshPropertyTokens->orientation].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_entityMeshProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

        TF_MAKE_STATIC_DATA(
            (_LeafPrimRelationshiphMap), _entityMeshRelationships)
        {
            std::vector<SdfPath> defaultMatList;
            defaultMatList.push_back(SdfPath("/Root/Materials/DefaultGolaemMat"));
            (*_entityMeshRelationships)[_entityMeshRelationshipTokens->materialBinding].defaultTargetPath = SdfPathListOp::CreateExplicit(defaultMatList);
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
            , _factory(new crowdio::SimulationCacheFactory())
        {
            usdplugin::init();
            _InitFromParams();
        }

        //-----------------------------------------------------------------------------
        GolaemUSD_DataImpl::~GolaemUSD_DataImpl()
        {
            delete _factory;
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
            if (path.IsPropertyPath()) // IsPropertyPath includes relational attributes
            {
                const TfToken& nameToken = path.GetNameToken();
                SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
                // A specific set of defined properties exist on the leaf prims only
                // as attributes. Non leaf prims have no properties.

                if (TfMapLookupPtr(*_entityProperties, nameToken) != NULL)
                {
                    const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                    if (entityData != NULL)
                    {
                        return SdfSpecTypeAttribute;
                    }
                }
                else
                {
                    const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                    if (entityData != NULL)
                    {
                        if (TfMapLookupPtr(entityData->ppAttrIndexes, nameToken) != NULL)
                        {
                            return SdfSpecTypeAttribute;
                        }
                    }
                }

                if (TfMapLookupPtr(*_entityMeshProperties, nameToken) != NULL)
                {
                    const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        return SdfSpecTypeAttribute;
                    }
                }
                else if (TfMapLookupPtr(*_entityMeshRelationships, nameToken) != NULL)
                {
                    const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        return SdfSpecTypeRelationship;
                    }
                }
                else
                {
                    const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        if (TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken) != NULL)
                        {
                            return SdfSpecTypeAttribute;
                        }
                    }
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
                else if (field == _geomCommonTokens->interpolation)
                {
                    return _HasPropertyInterpolation(path, value);
                }
                else if (field == SdfFieldKeys->TargetPaths)
                {
                    return _HasTargetPathValue(path, value);
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
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(_GetRootPrimPath().GetNameToken());
                }
                if (field == SdfFieldKeys->StartTimeCode)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(double(_startFrame));
                }
                if (field == SdfFieldKeys->EndTimeCode)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(double(_endFrame));
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
                    SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
                    // Only leaf prim properties have time samples
                    const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
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
                    const EntityData* entityData = TfMapLookupPtr(_entityDataMap, path);
                    if (entityData != NULL)
                    {
                        std::vector<TfToken> entityTokens = _entityPropertyTokens->allTokens;
                        // add pp attributes
                        for (const auto& itPPAttr : entityData->ppAttrIndexes)
                        {
                            entityTokens.push_back(itPPAttr.first);
                        }
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(entityTokens);
                    }
                    const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, path);
                    if (meshData != NULL)
                    {
                        std::vector<TfToken> meshTokens = _entityMeshPropertyTokens->allTokens;
                        meshTokens.insert(meshTokens.end(), _entityMeshRelationshipTokens->allTokens.begin(), _entityMeshRelationshipTokens->allTokens.end());
                        // add shader attributes
                        for (const auto& itShaderAttr : meshData->shaderAttrIndexes)
                        {
                            meshTokens.push_back(itShaderAttr.first);
                        }
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(meshTokens);
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

                for (const auto& itShaderAttr : it.second.ppAttrIndexes)
                {
                    if (!visitor->VisitSpec(data, it.first.AppendProperty(itShaderAttr.first)))
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
                for (const TfToken& propertyName : _entityMeshRelationshipTokens->allTokens)
                {
                    if (!visitor->VisitSpec(data, it.first.AppendProperty(propertyName)))
                    {
                        return;
                    }
                }

                for (const auto& itShaderAttr : it.second.shaderAttrIndexes)
                {
                    if (!visitor->VisitSpec(data, it.first.AppendProperty(itShaderAttr.first)))
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
                const TfToken& nameToken = path.GetNameToken();
                SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
                // For properties, check that it's a valid leaf prim property
                static std::vector<TfToken> animPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default,
                     SdfFieldKeys->TimeSamples});
                static std::vector<TfToken> nonAnimPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default});
                {
                    const _EntityPrimPropertyInfo* entityPropInfo = TfMapLookupPtr(*_entityProperties, nameToken);
                    if (entityPropInfo != NULL)
                    {
                        const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                        if (entityData != NULL)
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
                    else
                    {
                        const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                            if (ppAttrIdx != NULL)
                            {
                                // pp attributes are animated
                                return animPropFields;
                            }
                        }
                    }
                }
                {
                    const _EntityPrimPropertyInfo* entityMeshPropInfo = TfMapLookupPtr(*_entityMeshProperties, nameToken);
                    if (entityMeshPropInfo)
                    {
                        const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                        if (meshData != NULL)
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
                                         _geomCommonTokens->interpolation});
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
                                         _geomCommonTokens->interpolation});
                                    return nonAnimInterpPropFields;
                                }
                                return nonAnimPropFields;
                            }
                        }
                    }
                    else
                    {
                        const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                        if (meshData != NULL)
                        {
                            const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                            if (shaderAttrIdx != NULL)
                            {
                                // shader attributes are animated
                                return animPropFields;
                            }
                        }
                    }
                }
                {
                    const _EntityPrimRelationshipInfo* entityMeshRelInfo = TfMapLookupPtr(*_entityMeshRelationships, nameToken);
                    if (entityMeshRelInfo)
                    {
                        const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                        if (meshData != NULL)
                        {
                            static std::vector<TfToken> relationshipFields(
                                {SdfFieldKeys->TargetPaths});
                            return relationshipFields;
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
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            // Only leaf prim properties have time samples
            const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
            const EntityMeshData* meshData = NULL;
            if (entityData == NULL)
            {
                meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                entityData = meshData->entityData;
            }
            if (entityData == NULL || entityData->data.excluded)
            {
                return false;
            }

            _ComputeEntity(entityData, time);

            const TfToken& nameToken = path.GetNameToken();

            if (meshData == NULL)
            {
                // this is an entity node
                if (nameToken == _entityPropertyTokens->xformOpTranslate)
                {
                    // Animated position, anchored at the prim's layout position.
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.pos);
                }
                if (nameToken == _entityPropertyTokens->visibility)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                }
                const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                if (ppAttrIdx != NULL)
                {
                    if (value)
                    {
                        if (*ppAttrIdx < entityData->data.floatPPAttrValues.size())
                        {
                            // this is a float PP attribute
                            size_t floatAttrIdx = *ppAttrIdx;
                            *value = VtValue(entityData->data.floatPPAttrValues[floatAttrIdx]);
                        }
                        else
                        {
                            // this is a vector PP attribute
                            size_t vectAttrIdx = *ppAttrIdx - entityData->data.floatPPAttrValues.size();
                            *value = VtValue(entityData->data.vectorPPAttrValues[vectAttrIdx]);
                        }
                    }
                    return true;
                }
            }
            else
            {
                // this is a mesh node
                if (nameToken == _entityMeshPropertyTokens->points)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.points);
                }
                if (nameToken == _entityMeshPropertyTokens->normals)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.normals);
                }
                const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                if (shaderAttrIdx != NULL)
                {
                    if (value)
                    {
                        const CharacterShaderData& charShaderData = _shaderDataPerChar[entityData->characterIdx];
                        const glm::ShaderAttribute& shaderAttr = meshData->entityData->character->_shaderAttributes[*shaderAttrIdx];
                        switch (shaderAttr._type)
                        {
                        case glm::ShaderAttributeType::INT:
                        {
                            const size_t* intAttrIdx = TfMapLookupPtr(charShaderData._globalToIntShaderAttrIdx, *shaderAttrIdx);
                            if (intAttrIdx != NULL)
                            {
                                *value = VtValue(meshData->entityData->data.intShaderAttrValues[*intAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::FLOAT:
                        {
                            const size_t* floatAttrIdx = TfMapLookupPtr(charShaderData._globalToFloatShaderAttrIdx, *shaderAttrIdx);
                            if (floatAttrIdx != NULL)
                            {
                                *value = VtValue(meshData->entityData->data.floatShaderAttrValues[*floatAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::STRING:
                        {

                            const size_t* stringAttrIdx = TfMapLookupPtr(charShaderData._globalToStringShaderAttrIdx, *shaderAttrIdx);
                            if (stringAttrIdx != NULL)
                            {
                                *value = VtValue(meshData->entityData->data.stringShaderAttrValues[*stringAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::VECTOR:
                        {
                            const size_t* vectorAttrIdx = TfMapLookupPtr(charShaderData._globalToVectorShaderAttrIdx, *shaderAttrIdx);
                            if (vectorAttrIdx != NULL)
                            {
                                *value = VtValue(meshData->entityData->data.vectorShaderAttrValues[*vectorAttrIdx]);
                            }
                        }
                        break;
                        default:
                            break;
                        }
                    }
                    return true;
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
        glm::GlmString sanitizePrimName(const glm::GlmString& meshName)
        {
            glm::GlmString validMeshName = glm::replaceString(meshName, ":", "_");
            validMeshName = glm::replaceString(validMeshName, "|", "_");
            return validMeshName;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_InitFromParams()
        {
            _startFrame = 0;
            _endFrame = -1;

            glm::GlmString correctedFilePath;
            glm::Array<glm::GlmString> dirmapRules = glm::stringToStringArray(_params.glmDirmap.GetText(), ";");

            glm::crowdio::SimulationCacheLibrary simuCacheLibrary;
            findDirmappedFile(correctedFilePath, _params.glmCacheLibFile.GetText(), dirmapRules);
            loadSimulationCacheLib(simuCacheLibrary, correctedFilePath);

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
                GLM_CROWD_TRACE_WARNING("Could not find simulation cache item '" << _params.glmCacheLibItem.GetText() << "' in library file '" << _params.glmCacheLibFile.GetText() << "'");
                cacheInfo = &simuCacheLibrary.getCacheInformation(0);
            }

            if (cacheInfo != NULL)
            {
                cfNames = cacheInfo->_crowdFields;
                cacheName = cacheInfo->_cacheName;
                cacheDir = cacheInfo->_cacheDir;
                characterFiles = cacheInfo->_characterFiles;
                dstTerrainFile = cacheInfo->_destTerrain;
                enableLayout = cacheInfo->_enableLayout;
                layoutFiles = cacheInfo->_layoutFile;
                layoutFiles.trim(";");
            }
            // override cacheInfo params if neeeded
            if (!_params.glmCrowdFields.IsEmpty())
            {
                cfNames = _params.glmCrowdFields.GetText();
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
            if (!_params.glmTerrainFile.IsEmpty())
            {
                dstTerrainFile = _params.glmTerrainFile.GetText();
            }
            enableLayout = _params.glmEnableLayout;
            if (!_params.glmLayoutFiles.IsEmpty())
            {
                layoutFiles = _params.glmLayoutFiles.GetText();
            }

            float renderPercent = _params.glmRenderPercent * 0.01f;
            short geoTag = _params.glmGeometryTag;

            // terrain file
            glm::Array<glm::GlmString> crowdFieldNames = glm::stringToStringArray(cfNames.c_str(), ";");
            if (crowdFieldNames.size())
                srcTerrainFile = cacheDir + "/" + cacheName + "." + crowdFieldNames[0] + ".terrain.gtg";

            glm::GlmString materialPath = _params.glmMaterialPath.GetText();
            GolaemMaterialAssignMode::Value materialAssignMode = (GolaemMaterialAssignMode::Value)_params.glmMaterialAssignMode;

            GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;

            GlmString attributeNamespace = _params.glmAttributeNamespace.GetText();
            attributeNamespace.rtrim(":");

            // dirmap character files
            glm::Array<glm::GlmString> characterFilesList;
            split(characterFiles, ";", characterFilesList);
            for (size_t iCharFile = 0, charFileSize = characterFilesList.size(); iCharFile < charFileSize; ++iCharFile)
            {
                const glm::GlmString& characterFile = characterFilesList[iCharFile];
                findDirmappedFile(correctedFilePath, characterFile, dirmapRules);
                characterFilesList[iCharFile] = correctedFilePath;
            }
            characterFiles = glm::stringArrayToString(characterFilesList, ";");

            _factory->loadGolaemCharacters(characterFiles.c_str());

            glm::Array<glm::GlmString> layoutFilesArray = glm::stringToStringArray(layoutFiles, ";");
            size_t layoutCount = layoutFilesArray.size();
            if (enableLayout && layoutCount > 0)
            {
                for (size_t iLayout = 0; iLayout < layoutCount; ++iLayout)
                {
                    const glm::GlmString& layoutFile = layoutFilesArray[iLayout];
                    // dirmap layout file
                    findDirmappedFile(correctedFilePath, layoutFile, dirmapRules);
                    if (correctedFilePath.length() > 0)
                    {
                        _factory->loadLayoutHistoryFile(_factory->getLayoutHistoryCount(), correctedFilePath.c_str());
                    }
                }
            }

            glm::crowdio::crowdTerrain::TerrainMesh* sourceTerrain = NULL;
            glm::crowdio::crowdTerrain::TerrainMesh* destTerrain = NULL;
            if (!srcTerrainFile.empty())
            {
                // dirmap terrain file
                findDirmappedFile(correctedFilePath, srcTerrainFile, dirmapRules);
                sourceTerrain = glm::crowdio::crowdTerrain::loadTerrainAsset(correctedFilePath.c_str());
            }
            if (!dstTerrainFile.empty())
            {
                // dirmap terrain file
                findDirmappedFile(correctedFilePath, dstTerrainFile, dirmapRules);
                destTerrain = glm::crowdio::crowdTerrain::loadTerrainAsset(correctedFilePath.c_str());
            }
            if (destTerrain == NULL)
            {
                destTerrain = sourceTerrain;
            }
            _factory->setTerrainMeshes(sourceTerrain, destTerrain);

            // dirmap cache dir
            findDirmappedFile(correctedFilePath, cacheDir, dirmapRules);
            cacheDir = correctedFilePath;

            // Layer always has a root spec that is the default prim of the layer.
            _primSpecPaths.insert(_GetRootPrimPath());
            std::vector<TfToken>& rootChildNames = _primChildNames[_GetRootPrimPath()];

            _startFrame = -100000;
            _endFrame = 100000;

            _sgToSsPerChar.resize(_factory->getGolaemCharacters().size());
            _shaderDataPerChar.resize(_factory->getGolaemCharacters().size());
            _shaderAttrTypes.resize(ShaderAttributeType::END);
            _shaderAttrDefaultValues.resize(ShaderAttributeType::END);
            {
                int intValue = 0;
                VtValue value(intValue);
                _shaderAttrTypes[ShaderAttributeType::INT] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _shaderAttrDefaultValues[ShaderAttributeType::INT] = value;
            }
            {
                float floatValue = 0.1f;
                VtValue value(floatValue);
                _shaderAttrTypes[ShaderAttributeType::FLOAT] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _shaderAttrDefaultValues[ShaderAttributeType::FLOAT] = value;
            }
            {
                TfToken stringValue;
                VtValue value(stringValue);
                _shaderAttrTypes[ShaderAttributeType::STRING] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _shaderAttrDefaultValues[ShaderAttributeType::STRING] = value;
            }
            {
                GfVec3f vectorValue;
                VtValue value(vectorValue);
                _shaderAttrTypes[ShaderAttributeType::VECTOR] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _shaderAttrDefaultValues[ShaderAttributeType::VECTOR] = value;
            }
            // pp attributes have 2 possible types: float, vector
            _ppAttrTypes.resize(2);
            _ppAttrDefaultValues.resize(2);
            {
                float floatValue = 0.1f;
                VtValue value(floatValue);
                int attrTypeIdx = crowdio::GSC_PP_FLOAT - 1; // enum starts at 1
                _ppAttrTypes[attrTypeIdx] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _ppAttrDefaultValues[attrTypeIdx] = value;
            }
            {
                GfVec3f vectorValue;
                VtValue value(vectorValue);
                int attrTypeIdx = crowdio::GSC_PP_VECTOR - 1; // enum starts at 1
                _ppAttrTypes[attrTypeIdx] = SdfSchema::GetInstance().FindType(value).GetAsToken();
                _ppAttrDefaultValues[attrTypeIdx] = value;
            }
            glm::PODArray<int> intAttrCounters(_factory->getGolaemCharacters().size(), 0);
            glm::PODArray<int> floatAttrCounters(_factory->getGolaemCharacters().size(), 0);
            glm::PODArray<int> stringAttrCounters(_factory->getGolaemCharacters().size(), 0);
            glm::PODArray<int> vectorAttrCounters(_factory->getGolaemCharacters().size(), 0);
            for (int iChar = 0, charCount = _factory->getGolaemCharacters().sizeInt(); iChar < charCount; ++iChar)
            {
                glm::PODArray<int>& shadingGroupToSurfaceShader = _sgToSsPerChar[iChar];
                shadingGroupToSurfaceShader.clear();
                const glm::GolaemCharacter* character = _factory->getGolaemCharacter(iChar);
                if (character == NULL)
                {
                    continue;
                }

                shadingGroupToSurfaceShader.resize(character->_shadingGroups.size(), -1);
                for (size_t iSg = 0, sgCount = character->_shadingGroups.size(); iSg < sgCount; ++iSg)
                {
                    const glm::ShadingGroup& shadingGroup = character->_shadingGroups[iSg];
                    for (size_t iSa = 0, saCount = shadingGroup._shaderAssets.size(); iSa < saCount; ++iSa)
                    {
                        int shaderAssetIdx = shadingGroup._shaderAssets[iSa];
                        const glm::ShaderAsset& shaderAsset = character->_shaderAssets[shaderAssetIdx];
                        if (shaderAsset._category.find("surface") != glm::GlmString::npos)
                        {
                            shadingGroupToSurfaceShader[iSg] = shaderAssetIdx;
                            break;
                        }
                    }
                }

                CharacterShaderData& charShaderData = _shaderDataPerChar[iChar];
                int& intAttrCounter = intAttrCounters[iChar];
                int& floatAttrCounter = floatAttrCounters[iChar];
                int& stringAttrCounter = stringAttrCounters[iChar];
                int& vectorAttrCounter = vectorAttrCounters[iChar];
                for (size_t iShaderAttr = 0, shaderAttrCount = character->_shaderAttributes.size(); iShaderAttr < shaderAttrCount; ++iShaderAttr)
                {
                    const glm::ShaderAttribute& shaderAttr = character->_shaderAttributes[iShaderAttr];
                    switch (shaderAttr._type)
                    {
                    case glm::ShaderAttributeType::INT:
                    {
                        charShaderData._globalToIntShaderAttrIdx[iShaderAttr] = intAttrCounter;
                        ++intAttrCounter;
                    }
                    break;
                    case glm::ShaderAttributeType::FLOAT:
                    {
                        charShaderData._globalToFloatShaderAttrIdx[iShaderAttr] = floatAttrCounter;
                        ++floatAttrCounter;
                    }
                    break;
                    case glm::ShaderAttributeType::STRING:
                    {

                        charShaderData._globalToStringShaderAttrIdx[iShaderAttr] = stringAttrCounter;
                        ++stringAttrCounter;
                    }
                    break;
                    case glm::ShaderAttributeType::VECTOR:
                    {
                        charShaderData._globalToVectorShaderAttrIdx[iShaderAttr] = vectorAttrCounter;
                        ++vectorAttrCounter;
                    }
                    break;
                    default:
                        break;
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

                glm::crowdio::CachedSimulation& cachedSimulation = _factory->getCachedSimulation(cacheDir.c_str(), cacheName.c_str(), cfName.c_str());

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
                glm::crowdio::createEntityExclusionList(excludedEntities, cachedSimulation.getSrcSimulationData(), _factory->getLayoutHistories(), historyStructures);
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
                    entityData.data.inputGeoData._dirMapRules = dirmapRules;
                    entityData.data.inputGeoData._entityId = entityId;
                    entityData.data.inputGeoData._entityIndex = iEntity;
                    entityData.data.inputGeoData._cachedSimulation = &cachedSimulation;
                    entityData.data.inputGeoData._geometryTag = geoTag;
                    entityData.data.inputGeoData._fbxStorage = &getFbxStorage();
                    entityData.data.inputGeoData._fbxBaker = &getFbxBaker();
                    entityData.data.cachedSimulationLock = &cachedSimulationLock;

                    entityData.data.floatPPAttrValues.resize(simuData->_ppFloatAttributeCount, 0);
                    entityData.data.vectorPPAttrValues.resize(simuData->_ppVectorAttributeCount, GfVec3f(0));

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
                    const glm::GolaemCharacter* character = _factory->getGolaemCharacter(characterIdx);
                    if (character == NULL)
                    {
                        GLM_CROWD_TRACE_ERROR_LIMIT("The entity '" << entityId << "' has an invalid character index: '" << characterIdx << "'. Skipping it. Please assign a Rendering Type from the Rendering Attributes panel");
                        entityData.data.excluded = true;
                        continue;
                    }

                    entityData.data.intShaderAttrValues.resize(intAttrCounters[characterIdx], 0);
                    entityData.data.floatShaderAttrValues.resize(floatAttrCounters[characterIdx], 0);
                    entityData.data.stringShaderAttrValues.resize(stringAttrCounters[characterIdx]);
                    entityData.data.vectorShaderAttrValues.resize(vectorAttrCounters[characterIdx], GfVec3f(0));

                    entityData.character = character;
                    entityData.characterIdx = characterIdx;
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

                    glm::crowdio::OutputEntityGeoData outputData; // TODO: see if storage is better
                    _ComputeEntityMeshNames(entityMeshNames, outputData, &entityData);

                    size_t meshCount = entityMeshNames.size();
                    for (size_t iMesh = 0; iMesh < meshCount; ++iMesh)
                    {
                        TfToken meshName(sanitizePrimName(entityMeshNames[iMesh]).c_str());
                        SdfPath meshPath = entityPath.AppendChild(meshName);
                        _primSpecPaths.insert(meshPath);
                        entityChildNames.push_back(meshName);
                        EntityMeshData& meshData = _entityMeshDataMap[meshPath];
                        meshData.entityData = &entityData;
                        entityData.data.meshData.push_back(&meshData);
                    }

                    {
                        // initialize meshes

                        uint16_t entityType = simuData->_entityTypes[entityData.data.inputGeoData._entityIndex];

                        uint16_t boneCount = simuData->_boneCount[entityType];
                        entityData.data.bonePositionOffset = simuData->_iBoneOffsetPerEntityType[entityType] + simuData->_indexInEntityType[entityData.data.inputGeoData._entityIndex] * boneCount;
                        switch (displayMode)
                        {
                        case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                        {
                            // compute the bounding box of the current entity
                            glm::Vector3 halfExtents(1, 1, 1);
                            size_t geoIdx = 0;
                            const glm::GeometryAsset* geoAsset = entityData.character->getGeometryAsset(entityData.data.inputGeoData._geometryTag, geoIdx); // any LOD should have same extents !
                            if (geoAsset != NULL)
                            {
                                halfExtents = geoAsset->_halfExtentsYUp;
                            }
                            float characterScale = simuData->_scales[entityData.data.inputGeoData._entityIndex];
                            halfExtents *= characterScale;

                            // create the shape of the bounding box
                            EntityMeshData* bboxMeshData = entityData.data.meshData[0];
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

                            bboxMeshData->faceVertexCounts.resize(6);
                            for (size_t iFace = 0; iFace < 6; ++iFace)
                            {
                                bboxMeshData->faceVertexCounts[iFace] = 4;
                            }

                            // face 0
                            bboxMeshData->faceVertexIndices.push_back(0);
                            bboxMeshData->faceVertexIndices.push_back(1);
                            bboxMeshData->faceVertexIndices.push_back(2);
                            bboxMeshData->faceVertexIndices.push_back(3);

                            // face 1
                            bboxMeshData->faceVertexIndices.push_back(1);
                            bboxMeshData->faceVertexIndices.push_back(5);
                            bboxMeshData->faceVertexIndices.push_back(6);
                            bboxMeshData->faceVertexIndices.push_back(2);

                            // face 2
                            bboxMeshData->faceVertexIndices.push_back(2);
                            bboxMeshData->faceVertexIndices.push_back(6);
                            bboxMeshData->faceVertexIndices.push_back(7);
                            bboxMeshData->faceVertexIndices.push_back(3);

                            // face 3
                            bboxMeshData->faceVertexIndices.push_back(3);
                            bboxMeshData->faceVertexIndices.push_back(7);
                            bboxMeshData->faceVertexIndices.push_back(4);
                            bboxMeshData->faceVertexIndices.push_back(0);

                            // face 4
                            bboxMeshData->faceVertexIndices.push_back(0);
                            bboxMeshData->faceVertexIndices.push_back(4);
                            bboxMeshData->faceVertexIndices.push_back(5);
                            bboxMeshData->faceVertexIndices.push_back(1);

                            // face 5
                            bboxMeshData->faceVertexIndices.push_back(4);
                            bboxMeshData->faceVertexIndices.push_back(7);
                            bboxMeshData->faceVertexIndices.push_back(6);
                            bboxMeshData->faceVertexIndices.push_back(5);

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
                            glm::PODArray<int>& shadingGroupToSurfaceShader = _sgToSsPerChar[characterIdx];

                            for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                            {
                                EntityMeshData* meshData = entityData.data.meshData[iRenderMesh];
                                if (outputData._geoType == glm::crowdio::GeometryType::FBX)
                                {
                                    size_t iGeoFileMesh = outputData._meshAssetNameIndices[iRenderMesh];
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

                                    for (unsigned int iFbxPoly = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                    {
                                        if (polygonMasks[iFbxPoly])
                                        {
                                            int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                            meshData->faceVertexCounts.push_back(polySize);
                                            for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex)
                                            {
                                                // do not reverse polygon order
                                                int iFbxVertex = fbxMesh->GetPolygonVertex(iFbxPoly, iPolyVertex);
                                                int vertexId = vertexMasks[iFbxVertex];
                                                meshData->faceVertexIndices.push_back(vertexId);
                                            } // iPolyVertex
                                        }
                                    }
                                    meshData->data.normals.resize(meshData->faceVertexIndices.size());

                                    // find how many uv layers are available
                                    int uvSetCount = fbxMesh->GetLayerCount(FbxLayerElement::eUV);
                                    meshData->uvSets.resize(uvSetCount);
                                    FbxLayerElementUV* uvElement = NULL;
                                    for (int iUVSet = 0; iUVSet < uvSetCount; ++iUVSet)
                                    {
                                        VtVec2fArray& uvs = meshData->uvSets[iUVSet];
                                        uvs.resize(meshData->faceVertexIndices.size());
                                        FbxLayer* layer = fbxMesh->GetLayer(fbxMesh->GetLayerTypedIndex((int)iUVSet, FbxLayerElement::eUV));
                                        uvElement = layer->GetUVs();
                                        bool uvsByControlPoint = uvElement->GetMappingMode() == FbxLayerElement::eByControlPoint;
                                        bool uvReferenceDirect = uvElement->GetReferenceMode() == FbxLayerElement::eDirect;

                                        if (uvsByControlPoint)
                                        {
                                            int uvIndex = 0;
                                            for (unsigned int iFbxPoly = 0, actualIndexByPolyVertex = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                            {
                                                int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                                if (polygonMasks[iFbxPoly])
                                                {
                                                    for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex, ++actualIndexByPolyVertex)
                                                    {
                                                        // do not reverse polygon order
                                                        uvIndex = vertexMasks[fbxMesh->GetPolygonVertex(iFbxPoly, iPolyVertex)];
                                                        if (!uvReferenceDirect)
                                                        {
                                                            uvIndex = uvElement->GetIndexArray().GetAt(uvIndex);
                                                        }
                                                        FbxVector2 tempUV(uvElement->GetDirectArray().GetAt(uvIndex));
                                                        uvs[actualIndexByPolyVertex].Set((float)tempUV[0], (float)tempUV[1]);
                                                    }
                                                }
                                            }
                                        }
                                        else
                                        {
                                            int uvIndex = 0;
                                            for (unsigned int iFbxPoly = 0, actualIndexByPolyVertex = 0, fbxIndexByPolyVertex = 0; iFbxPoly < fbxPolyCount; ++iFbxPoly)
                                            {
                                                int polySize = fbxMesh->GetPolygonSize(iFbxPoly);
                                                if (polygonMasks[iFbxPoly])
                                                {
                                                    for (int iPolyVertex = 0; iPolyVertex < polySize; ++iPolyVertex, ++fbxIndexByPolyVertex, ++actualIndexByPolyVertex)
                                                    {
                                                        // do not reverse polygon order
                                                        uvIndex = fbxIndexByPolyVertex;
                                                        if (!uvReferenceDirect)
                                                        {
                                                            uvIndex = uvElement->GetIndexArray().GetAt(uvIndex);
                                                        }

                                                        FbxVector2 tempUV(uvElement->GetDirectArray().GetAt(uvIndex));
                                                        uvs[actualIndexByPolyVertex].Set((float)tempUV[0], (float)tempUV[1]);
                                                    } // iPolyVertex
                                                }
                                                else
                                                {
                                                    fbxIndexByPolyVertex += polySize;
                                                }
                                            } // iPoly
                                        }
                                    }
                                }
                                else if (outputData._geoType == glm::crowdio::GeometryType::GCG)
                                {
                                    glm::crowdio::GlmFileMeshTransform& assetFileMeshTransform = outputData._gcgCharacter->getGeometry()._transforms[outputData._transformIndicesInGcgFile[iRenderMesh]];
                                    glm::crowdio::GlmFileMesh& assetFileMesh = outputData._gcgCharacter->getGeometry()._meshes[assetFileMeshTransform._meshIndex];

                                    meshData->data.points.resize(assetFileMesh._vertexCount);

                                    for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                    {
                                        uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                        meshData->faceVertexCounts.push_back(polySize);
                                        for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                        {
                                            // do not reverse polygon order
                                            int vertexId = assetFileMesh._polygonsVertexIndices[iVertex];
                                            meshData->faceVertexIndices.push_back(vertexId);
                                        }
                                    }
                                    meshData->data.normals.resize(meshData->faceVertexIndices.size());

                                    meshData->uvSets.resize(assetFileMesh._uvSetCount);
                                    for (size_t iUVSet = 0; iUVSet < assetFileMesh._uvSetCount; ++iUVSet)
                                    {
                                        VtVec2fArray& uvs = meshData->uvSets[iUVSet];
                                        uvs.resize(meshData->faceVertexIndices.size());

                                        if (assetFileMesh._uvMode == glm::crowdio::GLM_UV_PER_CONTROL_POINT)
                                        {
                                            for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                            {
                                                uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                                for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                                {
                                                    // do not reverse polygon order
                                                    uint32_t uvIndex = assetFileMesh._polygonsVertexIndices[iVertex];
                                                    uvs[iVertex].Set(assetFileMesh._us[iUVSet][uvIndex], assetFileMesh._vs[iUVSet][uvIndex]);
                                                }
                                            }
                                        }
                                        else
                                        {
                                            for (uint32_t iPoly = 0, iVertex = 0; iPoly < assetFileMesh._polygonCount; ++iPoly)
                                            {
                                                uint32_t polySize = assetFileMesh._polygonsVertexCount[iPoly];
                                                for (uint32_t iPolyVtx = 0; iPolyVtx < polySize; ++iPolyVtx, ++iVertex)
                                                {
                                                    // do not reverse polygon order
                                                    uint32_t uvIndex = assetFileMesh._polygonsUVIndices[iVertex];
                                                    uvs[iVertex].Set(assetFileMesh._us[iUVSet][uvIndex], assetFileMesh._vs[iUVSet][uvIndex]);
                                                }
                                            }
                                        }
                                    }
                                }
                                int shadingGroupIdx = outputData._meshShadingGroups[iRenderMesh];

                                glm::GlmString materialName = "";
                                if (shadingGroupIdx >= 0)
                                {
                                    const glm::ShadingGroup& shGroup = outputData._character->_shadingGroups[shadingGroupIdx];
                                    materialName = materialPath;
                                    materialName.rtrim("/");
                                    materialName += "/";
                                    switch (materialAssignMode)
                                    {
                                    case GolaemMaterialAssignMode::BY_SHADING_GROUP:
                                    {
                                        materialName += shGroup._name;
                                    }
                                    break;
                                    case GolaemMaterialAssignMode::BY_SURFACE_SHADER:
                                    {
                                        // get the surface shader
                                        int shaderAssetIdx = shadingGroupToSurfaceShader[shadingGroupIdx];
                                        if (shaderAssetIdx >= 0)
                                        {
                                            const glm::ShaderAsset& shAsset = outputData._character->_shaderAssets[shaderAssetIdx];
                                            materialName += shAsset._name;
                                        }
                                        else
                                        {
                                            materialName += "DefaultGolaemMat";
                                        }
                                    }
                                    break;
                                    default:
                                        break;
                                    }
                                    materialName = sanitizePrimName(materialName);
                                    std::vector<SdfPath> pathArray;
                                    pathArray.push_back(SdfPath(materialName.c_str()));
                                    meshData->materialPath = SdfPathListOp::CreateExplicit(pathArray);

                                    // add shading group attributes
                                    for (size_t iShAttr = 0, shAttrCount = shGroup._shaderAttributes.size(); iShAttr < shAttrCount; ++iShAttr)
                                    {
                                        int shAttrIdx = shGroup._shaderAttributes[iShAttr];
                                        const glm::ShaderAttribute& shAttr = outputData._character->_shaderAttributes[shAttrIdx];
                                        GlmString attrName = shAttr._name;
                                        if (!attributeNamespace.empty())
                                        {
                                            attrName = attributeNamespace + ":" + attrName;
                                        }
                                        TfToken attrNameToken(attrName.c_str());
                                        meshData->shaderAttrIndexes[attrNameToken] = shAttrIdx;
                                    }
                                }
                                else
                                {
                                    meshData->materialPath = (*_entityMeshRelationships)[_entityMeshRelationshipTokens->materialBinding].defaultTargetPath;
                                }
                            }

                            // add pp attributes
                            size_t ppAttrIdx = 0;
                            for (uint8_t iFloatPPAttr = 0; iFloatPPAttr < simuData->_ppFloatAttributeCount; ++iFloatPPAttr, ++ppAttrIdx)
                            {
                                GlmString attrName = simuData->_ppFloatAttributeNames[iFloatPPAttr];
                                if (!attributeNamespace.empty())
                                {
                                    attrName = attributeNamespace + ":" + attrName;
                                }
                                TfToken attrNameToken(attrName.c_str());
                                entityData.ppAttrIndexes[attrNameToken] = ppAttrIdx;
                            }
                            for (uint8_t iVectPPAttr = 0; iVectPPAttr < simuData->_ppVectorAttributeCount; ++iVectPPAttr, ++ppAttrIdx)
                            {
                                GlmString attrName = simuData->_ppVectorAttributeNames[iVectPPAttr];
                                if (!attributeNamespace.empty())
                                {
                                    attrName = attributeNamespace + ":" + attrName;
                                }
                                TfToken attrNameToken(attrName.c_str());
                                entityData.ppAttrIndexes[attrNameToken] = ppAttrIdx;
                            }
                        }
                        break;
                        default:
                            break;
                        }
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
            if (!path.IsPrimPropertyPath())
            {
                return false;
            }
            const TfToken& nameToken = path.GetNameToken();
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            // Check that its one of our animated property names.
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, nameToken);
            if (propInfo != NULL)
            {
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                return propInfo->isAnimated && entityData != NULL;
            }
            else
            {
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                if (entityData != NULL)
                {
                    const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                    return ppAttrIdx != NULL;
                }
            }
            propInfo = TfMapLookupPtr(*_entityMeshProperties, nameToken);
            if (propInfo != NULL)
            {
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                return propInfo->isAnimated && meshData != NULL;
            }
            else
            {
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData != NULL)
                {
                    const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                    return shaderAttrIdx != NULL;
                }
            }
            return false;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_HasPropertyDefaultValue(const SdfPath& path, VtValue* value) const
        {
            // Check that it is a property id.
            if (!path.IsPrimPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const TfToken& nameToken = path.GetNameToken();
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, nameToken);
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                if (entityData)
                {
                    if (value)
                    {
                        // Special case for translate property. Each leaf prim has its own
                        // default position.
                        if (nameToken == _entityPropertyTokens->xformOpTranslate)
                        {
                            *value = VtValue(entityData->data.pos);
                        }
                        else if (nameToken == _entityPropertyTokens->visibility)
                        {
                            *value = VtValue(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                        }
                        else if (nameToken == _entityPropertyTokens->entityId)
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
            else
            {
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                if (entityData)
                {
                    const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                    if (ppAttrIdx != NULL)
                    {
                        if (value)
                        {
                            if (*ppAttrIdx < entityData->data.floatPPAttrValues.size())
                            {
                                // this is a float PP attribute
                                int attrTypeIdx = crowdio::GSC_PP_FLOAT - 1; // enum starts at 1
                                *value = _ppAttrDefaultValues[attrTypeIdx];
                            }
                            else
                            {
                                // this is a vector PP attribute
                                int attrTypeIdx = crowdio::GSC_PP_VECTOR - 1; // enum starts at 1
                                *value = _ppAttrDefaultValues[attrTypeIdx];
                            }
                        }
                        return true;
                    }
                }
            }
            propInfo = TfMapLookupPtr(*_entityMeshProperties, nameToken);
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData != NULL)
                {
                    if (value)
                    {
                        if (nameToken == _entityMeshPropertyTokens->points)
                        {
                            *value = VtValue(meshData->data.points);
                        }
                        else if (nameToken == _entityMeshPropertyTokens->normals)
                        {
                            *value = VtValue(meshData->data.normals);
                        }
                        else if (nameToken == _entityMeshPropertyTokens->faceVertexCounts)
                        {
                            *value = VtValue(meshData->faceVertexCounts);
                        }
                        else if (nameToken == _entityMeshPropertyTokens->faceVertexIndices)
                        {
                            *value = VtValue(meshData->faceVertexIndices);
                        }
                        else if (nameToken == _entityMeshPropertyTokens->uvs)
                        {
                            if (meshData->uvSets.empty())
                            {
                                return false;
                            }
                            *value = VtValue(meshData->uvSets.front());
                        }
                        else
                        {
                            *value = propInfo->defaultValue;
                        }
                    }
                    return true;
                }
            }
            else
            {
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData != NULL)
                {
                    const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                    if (shaderAttrIdx != NULL)
                    {
                        if (value)
                        {
                            const glm::ShaderAttribute& shaderAttr = meshData->entityData->character->_shaderAttributes[*shaderAttrIdx];
                            *value = _shaderAttrDefaultValues[shaderAttr._type];
                        }
                        return true;
                    }
                }
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        bool GolaemUSD_DataImpl::_HasTargetPathValue(const SdfPath& path, VtValue* value) const
        {
            // Check that it is a relationship id.
            if (!path.IsPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const TfToken& nameToken = path.GetNameToken();
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            const _EntityPrimRelationshipInfo* relInfo = TfMapLookupPtr(*_entityMeshRelationships, nameToken);
            if (relInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData)
                {
                    if (value)
                    {
                        if (nameToken == _entityMeshRelationshipTokens->materialBinding)
                        {
                            *value = VtValue(meshData->materialPath);
                        }
                        else
                        {
                            *value = VtValue(relInfo->defaultTargetPath);
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
            if (!path.IsPrimPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const TfToken& nameToken = path.GetNameToken();
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityMeshProperties, nameToken);
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the interpolation value
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData)
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
            if (!path.IsPrimPropertyPath())
            {
                return false;
            }

            // Check that it is one of our property names.
            const TfToken& nameToken = path.GetNameToken();
            SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
            const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_entityProperties, nameToken);
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the type name value
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                if (entityData)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(propInfo->typeName);
                }

                return false;
            }
            else
            {
                const EntityData* entityData = TfMapLookupPtr(_entityDataMap, primPath);
                if (entityData)
                {
                    const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                    if (ppAttrIdx != NULL)
                    {
                        if (value)
                        {
                            if (*ppAttrIdx < entityData->data.floatPPAttrValues.size())
                            {
                                // this is a float PP attribute
                                int attrTypeIdx = crowdio::GSC_PP_FLOAT - 1; // enum starts at 1
                                *value = TfToken(_ppAttrTypes[attrTypeIdx].c_str());
                            }
                            else
                            {
                                // this is a vector PP attribute
                                int attrTypeIdx = crowdio::GSC_PP_VECTOR - 1; // enum starts at 1
                                *value = TfToken(_ppAttrTypes[attrTypeIdx].c_str());
                            }
                        }
                        return true;
                    }
                }
            }

            propInfo = TfMapLookupPtr(*_entityMeshProperties, nameToken);
            if (propInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the type name value
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData)
                {
                    RETURN_TRUE_WITH_OPTIONAL_VALUE(propInfo->typeName);
                }

                return false;
            }
            else
            {
                const EntityMeshData* meshData = TfMapLookupPtr(_entityMeshDataMap, primPath);
                if (meshData)
                {
                    const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                    if (shaderAttrIdx != NULL)
                    {
                        const glm::ShaderAttribute& shaderAttr = meshData->entityData->character->_shaderAttributes[*shaderAttrIdx];
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken(_shaderAttrTypes[shaderAttr._type].c_str()));
                    }
                }
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeEntityMeshNames(glm::Array<glm::GlmString>& meshNames, glm::crowdio::OutputEntityGeoData& outputData, const EntityData* entityData) const
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
                const glm::ShaderAssetDataContainer* shaderDataContainer = NULL;
                {
                    glm::ScopedLock<glm::SpinLock> cachedSimuLock(*entityData->data.cachedSimulationLock);
                    simuData = entityData->data.inputGeoData._cachedSimulation->getFinalSimulationData();
                    frameData = entityData->data.inputGeoData._cachedSimulation->getFinalFrameData(time, UINT32_MAX, true);
                    shaderDataContainer = entityData->data.inputGeoData._cachedSimulation->getFinalShaderData(time, UINT32_MAX, true);
                }
                if (simuData == NULL || frameData == NULL)
                {
                    _InvalidateEntity(entityData);
                    return;
                }

                entityData->data.enabled = frameData->_entityEnabled[entityData->data.inputGeoData._entityIndex] == 1;
                if (!entityData->data.enabled)
                {
                    _InvalidateEntity(entityData);
                    return;
                }

                GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;
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
                    int intAttrCounter = 0;
                    int floatAttrCounter = 0;
                    int stringAttrCounter = 0;
                    int vectorAttrCounter = 0;
                    // compute shader data
                    const glm::Array<glm::GlmString>& shaderData = shaderDataContainer->data[entityData->data.inputGeoData._entityIndex];
                    glm::Vector3 vectValue;
                    for (size_t iShaderAttr = 0, shaderAttrCount = entityData->character->_shaderAttributes.size(); iShaderAttr < shaderAttrCount; iShaderAttr++)
                    {
                        const glm::GlmString& attrValueStr = shaderData[iShaderAttr];
                        const glm::ShaderAttribute& shaderAttr = entityData->character->_shaderAttributes[iShaderAttr];
                        switch (shaderAttr._type)
                        {
                        case glm::ShaderAttributeType::INT:
                        {
                            glm::fromString<int>(attrValueStr, entityData->data.intShaderAttrValues[intAttrCounter]);
                            ++intAttrCounter;
                        }
                        break;
                        case glm::ShaderAttributeType::FLOAT:
                        {
                            glm::fromString<float>(attrValueStr, entityData->data.floatShaderAttrValues[floatAttrCounter]);
                            ++floatAttrCounter;
                        }
                        break;
                        case glm::ShaderAttributeType::STRING:
                        {
                            entityData->data.stringShaderAttrValues[stringAttrCounter] = TfToken(attrValueStr.c_str());
                            ++stringAttrCounter;
                        }
                        break;
                        case glm::ShaderAttributeType::VECTOR:
                        {
                            glm::fromString(attrValueStr, vectValue);
                            entityData->data.vectorShaderAttrValues[vectorAttrCounter].Set(vectValue.getFloatValues());
                            ++vectorAttrCounter;
                        }
                        break;
                        default:
                            break;
                        }
                    }

                    // update pp attributes
                    for (uint8_t iFloatPPAttr = 0; iFloatPPAttr < simuData->_ppFloatAttributeCount; ++iFloatPPAttr)
                    {
                        entityData->data.floatPPAttrValues[iFloatPPAttr] = frameData->_ppFloatAttributeData[iFloatPPAttr][entityData->data.inputGeoData._entityIndex];
                    }
                    for (uint8_t iVectPPAttr = 0; iVectPPAttr < simuData->_ppVectorAttributeCount; ++iVectPPAttr)
                    {
                        entityData->data.vectorPPAttrValues[iVectPPAttr].Set(frameData->_ppVectorAttributeData[iVectPPAttr][entityData->data.inputGeoData._entityIndex]);
                    }

                    // update frame before computing geometry
                    entityData->data.inputGeoData._frames.resize(1);
                    entityData->data.inputGeoData._frames[0] = time;
                    entityData->data.inputGeoData._frameDatas.resize(1);
                    entityData->data.inputGeoData._frameDatas[0] = frameData;

                    glm::crowdio::OutputEntityGeoData outputData; // TODO: see if storage is better
                    glm::crowdio::GlmGeometryGenerationStatus geoStatus = glm::crowdio::glmPrepareEntityGeometry(&entityData->data.inputGeoData, &outputData);
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
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_InvalidateEntity(const EntityData* entityData) const
        {
            entityData->data.enabled = false;
            entityData->data.inputGeoData._frames.clear();
            entityData->data.inputGeoData._frameDatas.clear();
            entityData->data.intShaderAttrValues.clear();
            entityData->data.floatShaderAttrValues.clear();
            entityData->data.stringShaderAttrValues.clear();
            entityData->data.vectorShaderAttrValues.clear();
        }

    } // namespace usdplugin
} // namespace glm