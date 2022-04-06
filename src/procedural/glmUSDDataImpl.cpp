/***************************************************************************
 *                                                                          *
 *  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
 *                                                                          *
 ***************************************************************************/

#include "glmUSDDataImpl.h"

USD_INCLUDES_START
#include <pxr/pxr.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/reference.h>
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
#include <glmCrowdIOUtils.h>

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
            _skinMeshEntityPropertyTokens,
            ((xformOpOrder, "xformOpOrder"))
            ((xformOpTranslate, "xformOp:translate"))
            ((displayColor, "primvars:displayColor"))
            ((visibility, "visibility"))
            ((entityId, "entityId")));

        TF_DEFINE_PRIVATE_TOKENS(
            _skelEntityPropertyTokens,
            ((visibility, "visibility"))
            ((entityId, "entityId")));

        TF_DEFINE_PRIVATE_TOKENS(
            _skinMeshPropertyTokens,
            ((faceVertexCounts, "faceVertexCounts"))
            ((faceVertexIndices, "faceVertexIndices"))
            ((orientation, "orientation"))
            ((points, "points"))
            ((subdivisionScheme, "subdivisionScheme"))
            ((normals, "normals"))
            ((uvs, "primvars:st")));

        TF_DEFINE_PRIVATE_TOKENS(
            _skinMeshRelationshipTokens,
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
            // Most of our properties are animated.
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
            (_LeafPrimPropertyMap), _skinMeshEntityProperties)
        {

            // Define the default value types for our animated properties.
            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->xformOpTranslate].defaultValue = VtValue(GfVec3f(0));

            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->xformOpOrder].defaultValue = VtValue(VtTokenArray{_skinMeshEntityPropertyTokens->xformOpTranslate});
            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->xformOpOrder].isAnimated = false;

            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->displayColor].defaultValue = VtValue(VtVec3fArray({GfVec3f(1, 0.5, 0)}));
            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->displayColor].isAnimated = false;

            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->visibility].defaultValue = VtValue(_geomCommonTokens->inherited);

            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->entityId].defaultValue = VtValue(int64_t(-1));
            (*_skinMeshEntityProperties)[_skinMeshEntityPropertyTokens->entityId].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_skinMeshEntityProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _skelEntityProperties)
        {

            // Define the default value types for our animated properties.

            (*_skelEntityProperties)[_skelEntityPropertyTokens->visibility].defaultValue = VtValue(_geomCommonTokens->inherited);

            (*_skelEntityProperties)[_skelEntityPropertyTokens->entityId].defaultValue = VtValue(int64_t(-1));
            (*_skelEntityProperties)[_skelEntityPropertyTokens->entityId].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_skelEntityProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

        TF_MAKE_STATIC_DATA(
            (_LeafPrimPropertyMap), _skinMeshProperties)
        {
            // Define the default value types for our animated properties.
            (*_skinMeshProperties)[_skinMeshPropertyTokens->points].defaultValue = VtValue(VtVec3fArray());

            (*_skinMeshProperties)[_skinMeshPropertyTokens->normals].defaultValue = VtValue(VtVec3fArray());
            (*_skinMeshProperties)[_skinMeshPropertyTokens->normals].hasInterpolation = true;
            (*_skinMeshProperties)[_skinMeshPropertyTokens->normals].interpolation = _geomCommonTokens->faceVarying;

            // set the subdivision scheme to none in order to take normals into account
            (*_skinMeshProperties)[_skinMeshPropertyTokens->subdivisionScheme].defaultValue = _geomCommonTokens->none;
            (*_skinMeshProperties)[_skinMeshPropertyTokens->subdivisionScheme].isAnimated = false;

            (*_skinMeshProperties)[_skinMeshPropertyTokens->faceVertexCounts].defaultValue = VtValue(VtIntArray());
            (*_skinMeshProperties)[_skinMeshPropertyTokens->faceVertexCounts].isAnimated = false;
            (*_skinMeshProperties)[_skinMeshPropertyTokens->faceVertexIndices].defaultValue = VtValue(VtIntArray());
            (*_skinMeshProperties)[_skinMeshPropertyTokens->faceVertexIndices].isAnimated = false;

            (*_skinMeshProperties)[_skinMeshPropertyTokens->uvs].defaultValue = VtValue(VtVec2fArray());
            (*_skinMeshProperties)[_skinMeshPropertyTokens->uvs].isAnimated = false;
            (*_skinMeshProperties)[_skinMeshPropertyTokens->uvs].hasInterpolation = true;
            (*_skinMeshProperties)[_skinMeshPropertyTokens->uvs].interpolation = _geomCommonTokens->faceVarying;

            (*_skinMeshProperties)[_skinMeshPropertyTokens->orientation].defaultValue = VtValue(_geomCommonTokens->rightHanded);
            (*_skinMeshProperties)[_skinMeshPropertyTokens->orientation].isAnimated = false;

            // Use the schema to derive the type name tokens from each property's
            // default value.
            for (auto& it : *_skinMeshProperties)
            {
                it.second.typeName =
                    SdfSchema::GetInstance().FindType(it.second.defaultValue).GetAsToken();
            }
        }

        TF_MAKE_STATIC_DATA(
            (_LeafPrimRelationshiphMap), _skinMeshRelationships)
        {
            std::vector<SdfPath> defaultMatList;
            defaultMatList.push_back(SdfPath("/Root/Materials/DefaultGolaemMat"));
            (*_skinMeshRelationships)[_skinMeshRelationshipTokens->materialBinding].defaultTargetPath = SdfPathListOp::CreateExplicit(defaultMatList);
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
                if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                {
                    if (TfMapLookupPtr(*_skelEntityProperties, nameToken) != NULL)
                    {
                        const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            return SdfSpecTypeAttribute;
                        }
                    }
                    else
                    {
                        const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            if (TfMapLookupPtr(entityData->ppAttrIndexes, nameToken) != NULL)
                            {
                                return SdfSpecTypeAttribute;
                            }
                        }
                    }
                    {
                        const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
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
                    if (TfMapLookupPtr(*_skinMeshEntityProperties, nameToken) != NULL)
                    {
                        const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            return SdfSpecTypeAttribute;
                        }
                    }
                    else
                    {
                        const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            if (TfMapLookupPtr(entityData->ppAttrIndexes, nameToken) != NULL)
                            {
                                return SdfSpecTypeAttribute;
                            }
                        }
                    }

                    if (TfMapLookupPtr(*_skinMeshProperties, nameToken) != NULL)
                    {
                        const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                        if (meshData != NULL)
                        {
                            return SdfSpecTypeAttribute;
                        }
                    }
                    else if (TfMapLookupPtr(*_skinMeshRelationships, nameToken) != NULL)
                    {
                        const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                        if (meshData != NULL)
                        {
                            return SdfSpecTypeRelationship;
                        }
                    }
                    else
                    {
                        const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                        if (meshData != NULL)
                        {
                            if (TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken) != NULL)
                            {
                                return SdfSpecTypeAttribute;
                            }
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
                if (_primSpecPaths.find(path) != _primSpecPaths.end())
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
                        auto _MakeTimeSampleMap = [this, &path]()
                        {
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
                    if (_primSpecPaths.find(path) != _primSpecPaths.end())
                    {
                        if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                        {
                            if (TfMapLookupPtr(_skelEntityDataMap, path) != NULL || TfMapLookupPtr(_skelMeshDataMap, path) != NULL)
                            {
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(SdfSpecifierOver);
                            }
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(SdfSpecifierDef);
                        }
                        else
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(SdfSpecifierDef);
                        }
                    }
                }

                if (field == SdfFieldKeys->TypeName)
                {
                    // Only the leaf prim specs have a type name determined from the
                    // params.
                    if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                    {
                        if (TfMapLookupPtr(_skelEntityDataMap, path) != NULL || TfMapLookupPtr(_skelMeshDataMap, path) != NULL)
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken(""));
                        }
                    }
                    else
                    {
                        if (TfMapLookupPtr(_skinMeshEntityDataMap, path) != NULL)
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken("Xform"));
                        }

                        if (TfMapLookupPtr(_skinMeshDataMap, path) != NULL)
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(TfToken("Mesh"));
                        }
                    }
                }

                if (field == SdfFieldKeys->Active)
                {
                    SdfPath primPath = path.GetAbsoluteRootOrPrimPath();

                    if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                    {
                        {
                            const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                            if (entityData != NULL)
                            {
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(!entityData->data.excluded);
                            }
                        }

                        {
                            const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
                            if (meshData != NULL)
                            {
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.active);
                            }
                        }
                    }
                    else
                    {

                        {
                            const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                            if (entityData != NULL)
                            {
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(!entityData->data.excluded);
                            }
                        }
                    }
                }

                if (field == SdfFieldKeys->References)
                {
                    if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                    {
                        SdfPath primPath = path.GetAbsoluteRootOrPrimPath();
                        const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                        if (entityData != NULL)
                        {
                            RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.referencedUsdCharacter);
                        }
                    }
                }

                if (field == SdfChildrenKeys->PrimChildren)
                {
                    // Non-leaf prims have the prim children. The list is the same set
                    // of prim child names for each non-leaf prim regardless of depth.
                    if (_primSpecPaths.find(path) != _primSpecPaths.end())
                    {
                        if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                        {
                            if (TfMapLookupPtr(_skelMeshDataMap, path) == NULL)
                            {
                                const std::vector<TfToken>* childNames = TfMapLookupPtr(_primChildNames, path);
                                if (childNames != NULL)
                                {
                                    RETURN_TRUE_WITH_OPTIONAL_VALUE(*childNames);
                                }
                            }
                        }
                        else
                        {
                            if (TfMapLookupPtr(_skinMeshDataMap, path) == NULL)
                            {
                                const std::vector<TfToken>* childNames = TfMapLookupPtr(_primChildNames, path);
                                if (childNames != NULL)
                                {
                                    RETURN_TRUE_WITH_OPTIONAL_VALUE(*childNames);
                                }
                            }
                        }
                    }
                }

                if (field == SdfChildrenKeys->PropertyChildren)
                {
                    // Leaf prims have the same specified set of property children.
                    if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                    {
                        {
                            const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, path);
                            if (entityData != NULL)
                            {
                                std::vector<TfToken> entityTokens = _skelEntityPropertyTokens->allTokens;
                                // add pp attributes
                                for (const auto& itPPAttr : entityData->ppAttrIndexes)
                                {
                                    entityTokens.push_back(itPPAttr.first);
                                }
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(entityTokens);
                            }
                        }

                        {
                            const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, path);
                            if (meshData != NULL)
                            {
                                std::vector<TfToken> meshTokens;
                                // add shader attributes
                                for (const auto& itShaderAttr : meshData->shaderAttrIndexes)
                                {
                                    meshTokens.push_back(itShaderAttr.first);
                                }
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(meshTokens);
                            }
                        }
                    }
                    else
                    {
                        {
                            const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, path);
                            if (entityData != NULL)
                            {
                                std::vector<TfToken> entityTokens = _skinMeshEntityPropertyTokens->allTokens;
                                // add pp attributes
                                for (const auto& itPPAttr : entityData->ppAttrIndexes)
                                {
                                    entityTokens.push_back(itPPAttr.first);
                                }
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(entityTokens);
                            }
                        }

                        {
                            const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, path);
                            if (meshData != NULL)
                            {
                                std::vector<TfToken> meshTokens = _skinMeshPropertyTokens->allTokens;
                                meshTokens.insert(meshTokens.end(), _skinMeshRelationshipTokens->allTokens.begin(), _skinMeshRelationshipTokens->allTokens.end());
                                // add shader attributes
                                for (const auto& itShaderAttr : meshData->shaderAttrIndexes)
                                {
                                    meshTokens.push_back(itShaderAttr.first);
                                }
                                RETURN_TRUE_WITH_OPTIONAL_VALUE(meshTokens);
                            }
                        }
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
            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
                // Visit the property specs which exist only on entity prims.
                for (auto it : _skelEntityDataMap)
                {
                    for (const TfToken& propertyName : _skelEntityPropertyTokens->allTokens)
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
                for (auto it : _skelMeshDataMap)
                {
                    for (const auto& itShaderAttr : it.second.shaderAttrIndexes)
                    {
                        if (!visitor->VisitSpec(data, it.first.AppendProperty(itShaderAttr.first)))
                        {
                            return;
                        }
                    }
                }
            }
            else
            {
                // Visit the property specs which exist only on entity prims.
                for (auto it : _skinMeshEntityDataMap)
                {
                    for (const TfToken& propertyName : _skinMeshEntityPropertyTokens->allTokens)
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
                for (auto it : _skinMeshDataMap)
                {
                    for (const TfToken& propertyName : _skinMeshPropertyTokens->allTokens)
                    {
                        if (!visitor->VisitSpec(data, it.first.AppendProperty(propertyName)))
                        {
                            return;
                        }
                    }
                    for (const TfToken& propertyName : _skinMeshRelationshipTokens->allTokens)
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
                static std::vector<TfToken> animInterpPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default,
                     SdfFieldKeys->TimeSamples,
                     _geomCommonTokens->interpolation});
                static std::vector<TfToken> nonAnimInterpPropFields(
                    {SdfFieldKeys->TypeName,
                     SdfFieldKeys->Default,
                     _geomCommonTokens->interpolation});
                {
                    if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                    {
                        const _EntityPrimPropertyInfo* entityPropInfo = TfMapLookupPtr(*_skelEntityProperties, nameToken);
                        if (entityPropInfo != NULL)
                        {
                            const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
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
                            const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
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

                        {
                            const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
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
                    else
                    {
                        const _EntityPrimPropertyInfo* entityPropInfo = TfMapLookupPtr(*_skinMeshEntityProperties, nameToken);
                        if (entityPropInfo != NULL)
                        {
                            const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
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
                            const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
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

                        {
                            const _EntityPrimPropertyInfo* entityMeshPropInfo = TfMapLookupPtr(*_skinMeshProperties, nameToken);
                            if (entityMeshPropInfo)
                            {
                                const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                                if (meshData != NULL)
                                {
                                    // Include time sample field in the property is animated.
                                    if (entityMeshPropInfo->isAnimated)
                                    {
                                        if (entityMeshPropInfo->hasInterpolation)
                                        {
                                            return animInterpPropFields;
                                        }
                                        return animPropFields;
                                    }
                                    else
                                    {
                                        if (entityMeshPropInfo->hasInterpolation)
                                        {
                                            return nonAnimInterpPropFields;
                                        }
                                        return nonAnimPropFields;
                                    }
                                }
                            }
                            else
                            {
                                const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
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
                            const _EntityPrimRelationshipInfo* entityMeshRelInfo = TfMapLookupPtr(*_skinMeshRelationships, nameToken);
                            if (entityMeshRelInfo)
                            {
                                const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                                if (meshData != NULL)
                                {
                                    static std::vector<TfToken> relationshipFields(
                                        {SdfFieldKeys->TargetPaths});
                                    return relationshipFields;
                                }
                            }
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
            else if (_primSpecPaths.find(path) != _primSpecPaths.end())
            {

                // Prim spec. Different fields for leaf and non-leaf prims.
                if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
                {
                    if (TfMapLookupPtr(_skelEntityDataMap, path) != NULL)
                    {
                        static std::vector<TfToken> entityPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfFieldKeys->TypeName,
                             SdfFieldKeys->Active,
                             SdfChildrenKeys->PrimChildren,
                             SdfChildrenKeys->PropertyChildren,
                             SdfFieldKeys->References});
                        return entityPrimFields;
                    }
                    else if (TfMapLookupPtr(_skelMeshDataMap, path) != NULL)
                    {
                        static std::vector<TfToken> meshPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfFieldKeys->TypeName,
                             SdfChildrenKeys->PropertyChildren});
                        return meshPrimFields;
                    }
                    else
                    {
                        static std::vector<TfToken> nonLeafPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfChildrenKeys->PrimChildren});
                        return nonLeafPrimFields;
                    }
                }
                else
                {
                    // Prim spec. Different fields for leaf and non-leaf prims.
                    if (TfMapLookupPtr(_skinMeshEntityDataMap, path) != NULL)
                    {
                        static std::vector<TfToken> entityPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfFieldKeys->TypeName,
                             SdfFieldKeys->Active,
                             SdfChildrenKeys->PrimChildren,
                             SdfChildrenKeys->PropertyChildren});
                        return entityPrimFields;
                    }
                    else if (TfMapLookupPtr(_skinMeshDataMap, path) != NULL)
                    {
                        static std::vector<TfToken> meshPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfFieldKeys->TypeName,
                             SdfChildrenKeys->PropertyChildren});
                        return meshPrimFields;
                    }
                    else
                    {
                        static std::vector<TfToken> nonLeafPrimFields(
                            {SdfFieldKeys->Specifier,
                             SdfChildrenKeys->PrimChildren});
                        return nonLeafPrimFields;
                    }
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
            const TfToken& nameToken = path.GetNameToken();

            bool isEntityPath = true;
            EntityVolatileData* entityVolatileData = NULL;
            const EntityData* genericEntityData = NULL;
            const size_t* shaderAttrIdx = NULL;
            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
                const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                const SkelMeshData* meshData = NULL;
                if (entityData == NULL)
                {
                    meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
                    entityData = meshData->entityData;
                }
                if (entityData == NULL || entityData->data.excluded)
                {
                    return false;
                }

                _ComputeSkelEntity(entityData, time);
                entityVolatileData = &entityData->data;
                genericEntityData = entityData;
                
                isEntityPath = meshData == NULL;

                if (meshData == NULL)
                {
                    // this is an entity node
                    if (nameToken == _skelEntityPropertyTokens->visibility)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                    }
                }
                else
                {
                    shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                }
            }
            else
            {
                // Only leaf prim properties have time samples
                const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                const SkinMeshData* meshData = NULL;
                if (entityData == NULL)
                {
                    meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                    entityData = meshData->entityData;
                }
                if (entityData == NULL || entityData->data.excluded)
                {
                    return false;
                }

                _ComputeSkinMeshEntity(entityData, time);
                entityVolatileData = &entityData->data;
                genericEntityData = entityData;

                isEntityPath = meshData == NULL;

                if (meshData == NULL)
                {
                    // this is an entity node
                    if (nameToken == _skinMeshEntityPropertyTokens->xformOpTranslate)
                    {
                        // Animated position, anchored at the prim's layout position.
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.pos);
                    }
                    if (nameToken == _skinMeshEntityPropertyTokens->visibility)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                    }
                }
                else
                {
                    // this is a mesh node
                    if (nameToken == _skinMeshPropertyTokens->points)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.points);
                    }
                    if (nameToken == _skinMeshPropertyTokens->normals)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(meshData->data.normals);
                    }
                    shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                }
            }

            if (isEntityPath)
            {
                const size_t* ppAttrIdx = TfMapLookupPtr(genericEntityData->ppAttrIndexes, nameToken);
                if (ppAttrIdx != NULL)
                {
                    if (value)
                    {
                        if (*ppAttrIdx < entityVolatileData->floatPPAttrValues.size())
                        {
                            // this is a float PP attribute
                            size_t floatAttrIdx = *ppAttrIdx;
                            *value = VtValue(entityVolatileData->floatPPAttrValues[floatAttrIdx]);
                        }
                        else
                        {
                            // this is a vector PP attribute
                            size_t vectAttrIdx = *ppAttrIdx - entityVolatileData->floatPPAttrValues.size();
                            *value = VtValue(entityVolatileData->vectorPPAttrValues[vectAttrIdx]);
                        }
                    }
                    return true;
                }
            }
            else
            {
                if (shaderAttrIdx != NULL)
                {
                    if (value)
                    {
                        const CharacterShaderData& charShaderData = _shaderDataPerChar[genericEntityData->characterIdx];
                        const glm::ShaderAttribute& shaderAttr = genericEntityData->character->_shaderAttributes[*shaderAttrIdx];
                        switch (shaderAttr._type)
                        {
                        case glm::ShaderAttributeType::INT:
                        {
                            const size_t* intAttrIdx = TfMapLookupPtr(charShaderData._globalToIntShaderAttrIdx, *shaderAttrIdx);
                            if (intAttrIdx != NULL)
                            {
                                *value = VtValue(entityVolatileData->intShaderAttrValues[*intAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::FLOAT:
                        {
                            const size_t* floatAttrIdx = TfMapLookupPtr(charShaderData._globalToFloatShaderAttrIdx, *shaderAttrIdx);
                            if (floatAttrIdx != NULL)
                            {
                                *value = VtValue(entityVolatileData->floatShaderAttrValues[*floatAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::STRING:
                        {

                            const size_t* stringAttrIdx = TfMapLookupPtr(charShaderData._globalToStringShaderAttrIdx, *shaderAttrIdx);
                            if (stringAttrIdx != NULL)
                            {
                                *value = VtValue(entityVolatileData->stringShaderAttrValues[*stringAttrIdx]);
                            }
                        }
                        break;
                        case glm::ShaderAttributeType::VECTOR:
                        {
                            const size_t* vectorAttrIdx = TfMapLookupPtr(charShaderData._globalToVectorShaderAttrIdx, *shaderAttrIdx);
                            if (vectorAttrIdx != NULL)
                            {
                                *value = VtValue(entityVolatileData->vectorShaderAttrValues[*vectorAttrIdx]);
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
            validMeshName = glm::replaceString(validMeshName, ".", "_");
            validMeshName = glm::replaceString(validMeshName, ";", "_");
            validMeshName = glm::replaceString(validMeshName, "\\", "_");
            validMeshName = glm::replaceString(validMeshName, "-", "_");
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

            glm::GlmString usdCharacterFiles;

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

            if (!_params.glmUsdCharacterFiles.IsEmpty())
            {
                usdCharacterFiles = _params.glmUsdCharacterFiles.GetText();
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
            {
                glm::Array<glm::GlmString> characterFilesList;
                split(characterFiles, ";", characterFilesList);
                for (size_t iCharFile = 0, charFileSize = characterFilesList.size(); iCharFile < charFileSize; ++iCharFile)
                {
                    const glm::GlmString& characterFile = characterFilesList[iCharFile];
                    findDirmappedFile(correctedFilePath, characterFile, dirmapRules);
                    characterFilesList[iCharFile] = correctedFilePath;
                }
                characterFiles = glm::stringArrayToString(characterFilesList, ";");
            }

            glm::Array<glm::GlmString> usdCharacterFilesList;
            split(usdCharacterFiles, ";", usdCharacterFilesList);
            for (size_t iCharFile = 0, charFileSize = usdCharacterFilesList.size(); iCharFile < charFileSize; ++iCharFile)
            {
                const glm::GlmString& usdCharacterFile = usdCharacterFilesList[iCharFile];
                findDirmappedFile(correctedFilePath, usdCharacterFile, dirmapRules);
                usdCharacterFilesList[iCharFile] = correctedFilePath;
            }

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
                    int shaderAssetIdx = character->findShaderAsset(shadingGroup, "surface");
                    if (shaderAssetIdx >= 0)
                    {
                        shadingGroupToSurfaceShader[iSg] = shaderAssetIdx;
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
                const glm::GlmString& glmCfName = crowdFieldNames[iCf];
                if (glmCfName.empty())
                {
                    continue;
                }

                TfToken cfName(sanitizePrimName(glmCfName).c_str());

                SdfPath cfPath = _GetRootPrimPath().AppendChild(cfName);
                _primSpecPaths.insert(cfPath);
                rootChildNames.push_back(cfName);

                std::vector<TfToken>& cfChildNames = _primChildNames[cfPath];

                glm::crowdio::CachedSimulation& cachedSimulation = _factory->getCachedSimulation(cacheDir.c_str(), cacheName.c_str(), glmCfName.c_str());

                const glm::crowdio::GlmSimulationData* simuData = cachedSimulation.getFinalSimulationData();

                if (simuData == NULL)
                {
                    continue;
                }

                // create lock for cached simulation
                glm::SpinLock& cachedSimulationLock = _cachedSimulationLocks[&cachedSimulation];

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

                    EntityData* entityData = NULL;
                    EntityVolatileData* volatileData = NULL;
                    SkinMeshEntityData* skinMeshEntityData = NULL;
                    SkelEntityData* skelEntityData = NULL;
                    if (displayMode == GolaemDisplayMode::SKELETON)
                    {
                        skelEntityData = &_skelEntityDataMap[entityPath];
                        entityData = skelEntityData;
                        volatileData = &skelEntityData->data;
                    }
                    else
                    {
                        skinMeshEntityData = &_skinMeshEntityDataMap[entityPath];
                        entityData = skinMeshEntityData;
                        volatileData = &skinMeshEntityData->data;

                        skinMeshEntityData->data.inputGeoData._fbxStorage = &getFbxStorage();
                        skinMeshEntityData->data.inputGeoData._fbxBaker = &getFbxBaker();
                        volatileData->inputGeoData._geometryTag = geoTag;
                    }
                    volatileData->inputGeoData._dirMapRules = dirmapRules;
                    volatileData->inputGeoData._entityId = entityId;
                    volatileData->inputGeoData._entityIndex = iEntity;
                    volatileData->inputGeoData._cachedSimulation = &cachedSimulation;

                    // volatileData->inputGeoData._frames.resize(1);
                    // volatileData->inputGeoData._frames[0] = _startFrame;
                    // volatileData->inputGeoData._frameDatas.resize(1);
                    // volatileData->inputGeoData._frameDatas[0] = cachedSimulation.getFinalFrameData(_startFrame, UINT32_MAX, true);

                    volatileData->computedTimeSample = _startFrame - 1; // ensure there will be a compute in QueryTimeSample

                    volatileData->cachedSimulationLock = &cachedSimulationLock;

                    volatileData->floatPPAttrValues.resize(simuData->_ppFloatAttributeCount, 0);
                    volatileData->vectorPPAttrValues.resize(simuData->_ppVectorAttributeCount, GfVec3f(0));

                    bool excludedEntity = iEntity >= maxEntities;

                    volatileData->excluded = excludedEntity;

                    if (excludedEntity)
                    {
                        continue;
                    }

                    int32_t characterIdx = simuData->_characterIdx[iEntity];
                    const glm::GolaemCharacter* character = _factory->getGolaemCharacter(characterIdx);
                    if (character == NULL)
                    {
                        GLM_CROWD_TRACE_ERROR_LIMIT("The entity '" << entityId << "' has an invalid character index: '" << characterIdx << "'. Skipping it. Please assign a Rendering Type from the Rendering Attributes panel");
                        volatileData->excluded = true;
                        continue;
                    }

                    volatileData->intShaderAttrValues.resize(intAttrCounters[characterIdx], 0);
                    volatileData->floatShaderAttrValues.resize(floatAttrCounters[characterIdx], 0);
                    volatileData->stringShaderAttrValues.resize(stringAttrCounters[characterIdx]);
                    volatileData->vectorShaderAttrValues.resize(vectorAttrCounters[characterIdx], GfVec3f(0));

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
                        entityData->ppAttrIndexes[attrNameToken] = ppAttrIdx;
                    }
                    for (uint8_t iVectPPAttr = 0; iVectPPAttr < simuData->_ppVectorAttributeCount; ++iVectPPAttr, ++ppAttrIdx)
                    {
                        GlmString attrName = simuData->_ppVectorAttributeNames[iVectPPAttr];
                        if (!attributeNamespace.empty())
                        {
                            attrName = attributeNamespace + ":" + attrName;
                        }
                        TfToken attrNameToken(attrName.c_str());
                        entityData->ppAttrIndexes[attrNameToken] = ppAttrIdx;
                    }

                    entityData->character = character;
                    entityData->characterIdx = characterIdx;
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

                    uint16_t entityType = simuData->_entityTypes[volatileData->inputGeoData._entityIndex];

                    uint16_t boneCount = simuData->_boneCount[entityType];
                    volatileData->bonePositionOffset = simuData->_iBoneOffsetPerEntityType[entityType] + simuData->_indexInEntityType[volatileData->inputGeoData._entityIndex] * boneCount;
                    std::vector<TfToken>& entityChildNames = _primChildNames[entityPath];
                    if (displayMode == GolaemDisplayMode::SKELETON)
                    {
                        if (characterIdx < usdCharacterFilesList.size())
                        {
                            const glm::GlmString& usdCharacterFile = usdCharacterFilesList[characterIdx];
                            SdfReferenceVector referenceArray;
                            referenceArray.push_back(SdfReference(usdCharacterFile.c_str()));
                            skelEntityData->data.referencedUsdCharacter.SetAppendedItems(referenceArray);
                        }

                        PODArray<bool> activeMeshes;
                        glm::Array<PODArray<int>> meshShadingGroups;
                        // compute mesh names
                        _ComputeEntityMeshNames(entityMeshNames, activeMeshes, meshShadingGroups, skelEntityData);

                        size_t meshCount = entityMeshNames.size();
                        for (size_t iMesh = 0; iMesh < meshCount; ++iMesh)
                        {
                            TfToken meshName(sanitizePrimName(entityMeshNames[iMesh]).c_str());
                            SdfPath meshPath = entityPath.AppendChild(meshName);
                            _primSpecPaths.insert(meshPath);
                            entityChildNames.push_back(meshName);
                            SkelMeshData& meshData = _skelMeshDataMap[meshPath];
                            meshData.entityData = skelEntityData;
                            meshData.data.active = activeMeshes[iMesh];
                            skelEntityData->data.meshData.push_back(&meshData);

                            PODArray<int>& shadingGroups = meshShadingGroups[iMesh];
                            for (size_t iShGroup = 0, shGroupCount = shadingGroups.size(); iShGroup < shGroupCount; ++iShGroup)
                            {
                                const ShadingGroup& shGroup = character->_shadingGroups[shadingGroups[iShGroup]];
                                for (size_t iShAttr = 0, shAttrCount = shGroup._shaderAttributes.size(); iShAttr < shAttrCount; ++iShAttr)
                                {
                                    int shAttrIdx = shGroup._shaderAttributes[iShAttr];
                                    const glm::ShaderAttribute& shAttr = character->_shaderAttributes[shAttrIdx];
                                    GlmString attrName = shAttr._name;
                                    if (!attributeNamespace.empty())
                                    {
                                        attrName = attributeNamespace + ":" + attrName;
                                    }
                                    TfToken attrNameToken(attrName.c_str());
                                    meshData.shaderAttrIndexes[attrNameToken] = shAttrIdx;
                                }
                            }
                        }
                    }
                    else
                    {
                        glm::crowdio::OutputEntityGeoData outputData; // TODO: see if storage is better
                        _ComputeEntityMeshNames(entityMeshNames, outputData, skinMeshEntityData);

                        size_t meshCount = entityMeshNames.size();
                        for (size_t iMesh = 0; iMesh < meshCount; ++iMesh)
                        {
                            TfToken meshName(sanitizePrimName(entityMeshNames[iMesh]).c_str());
                            SdfPath meshPath = entityPath.AppendChild(meshName);
                            _primSpecPaths.insert(meshPath);
                            entityChildNames.push_back(meshName);
                            SkinMeshData& meshData = _skinMeshDataMap[meshPath];
                            meshData.entityData = skinMeshEntityData;
                            skinMeshEntityData->data.meshData.push_back(&meshData);
                        }

                        // initialize meshes

                        switch (displayMode)
                        {
                        case glm::usdplugin::GolaemDisplayMode::BOUNDING_BOX:
                        {
                            // compute the bounding box of the current entity
                            glm::Vector3 halfExtents(1, 1, 1);
                            size_t geoIdx = 0;
                            const glm::GeometryAsset* geoAsset = entityData->character->getGeometryAsset(volatileData->inputGeoData._geometryTag, geoIdx); // any LOD should have same extents !
                            if (geoAsset != NULL)
                            {
                                halfExtents = geoAsset->_halfExtentsYUp;
                            }
                            float characterScale = simuData->_scales[volatileData->inputGeoData._entityIndex];
                            halfExtents *= characterScale;

                            // create the shape of the bounding box
                            SkinMeshData* bboxMeshData = skinMeshEntityData->data.meshData[0];
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
                        case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                        {
                            glm::PODArray<int>& shadingGroupToSurfaceShader = _sgToSsPerChar[characterIdx];

                            for (size_t iRenderMesh = 0; iRenderMesh < meshCount; ++iRenderMesh)
                            {
                                SkinMeshData* meshData = skinMeshEntityData->data.meshData[iRenderMesh];
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
                                    meshData->materialPath = (*_skinMeshRelationships)[_skinMeshRelationshipTokens->materialBinding].defaultTargetPath;
                                }
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

            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
                // Check that its one of our animated property names.
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skelEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                    return propInfo->isAnimated && entityData != NULL;
                }

                {
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                    if (entityData != NULL)
                    {
                        const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                        return ppAttrIdx != NULL;
                    }
                }

                {
                    const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                        return shaderAttrIdx != NULL;
                    }
                }
            }
            else
            {
                // Check that its one of our animated property names.
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skinMeshEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                    return propInfo->isAnimated && entityData != NULL;
                }
                else
                {
                    propInfo = TfMapLookupPtr(*_skinMeshProperties, nameToken);
                }
                if (propInfo != NULL)
                {
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                    return propInfo->isAnimated && meshData != NULL;
                }
                
                {
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                    if (entityData != NULL)
                    {
                        const size_t* ppAttrIdx = TfMapLookupPtr(entityData->ppAttrIndexes, nameToken);
                        return ppAttrIdx != NULL;
                    }
                }
               
                {
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        const size_t* shaderAttrIdx = TfMapLookupPtr(meshData->shaderAttrIndexes, nameToken);
                        return shaderAttrIdx != NULL;
                    }
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
            // Check that it belongs to a leaf prim before getting the default value
            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skelEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                    if (entityData)
                    {
                        if (value)
                        {
                            if (nameToken == _skelEntityPropertyTokens->visibility)
                            {
                                *value = VtValue(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                            }
                            else if (nameToken == _skelEntityPropertyTokens->entityId)
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
                }
                else
                {
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
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
                {
                    const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
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
            }
            else
            {
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skinMeshEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                    if (entityData)
                    {
                        if (value)
                        {
                            // Special case for translate property. Each leaf prim has its own
                            // default position.
                            if (nameToken == _skinMeshEntityPropertyTokens->xformOpTranslate)
                            {
                                *value = VtValue(entityData->data.pos);
                            }
                            else if (nameToken == _skinMeshEntityPropertyTokens->visibility)
                            {
                                *value = VtValue(entityData->data.enabled ? _geomCommonTokens->inherited : _geomCommonTokens->invisible);
                            }
                            else if (nameToken == _skinMeshEntityPropertyTokens->entityId)
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
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
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
                propInfo = TfMapLookupPtr(*_skinMeshProperties, nameToken);
                if (propInfo != NULL)
                {
                    // Check that it belongs to a leaf prim before getting the default value
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                    if (meshData != NULL)
                    {
                        if (value)
                        {
                            if (nameToken == _skinMeshPropertyTokens->points)
                            {
                                *value = VtValue(meshData->data.points);
                            }
                            else if (nameToken == _skinMeshPropertyTokens->normals)
                            {
                                *value = VtValue(meshData->data.normals);
                            }
                            else if (nameToken == _skinMeshPropertyTokens->faceVertexCounts)
                            {
                                *value = VtValue(meshData->faceVertexCounts);
                            }
                            else if (nameToken == _skinMeshPropertyTokens->faceVertexIndices)
                            {
                                *value = VtValue(meshData->faceVertexIndices);
                            }
                            else if (nameToken == _skinMeshPropertyTokens->uvs)
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
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
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
            const _EntityPrimRelationshipInfo* relInfo = TfMapLookupPtr(*_skinMeshRelationships, nameToken);
            if (relInfo != NULL)
            {
                // Check that it belongs to a leaf prim before getting the default value
                const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                if (meshData)
                {
                    if (value)
                    {
                        if (nameToken == _skinMeshRelationshipTokens->materialBinding)
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
            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
            }
            else
            {
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skinMeshProperties, nameToken);
                if (propInfo != NULL)
                {
                    // Check that it belongs to a leaf prim before getting the interpolation value
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
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
            if (_params.glmDisplayMode == GolaemDisplayMode::SKELETON)
            {
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skelEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    // Check that it belongs to a leaf prim before getting the type name value
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
                    if (entityData)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(propInfo->typeName);
                    }

                    return false;
                }

                {
                    const SkelEntityData* entityData = TfMapLookupPtr(_skelEntityDataMap, primPath);
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

                {
                    const SkelMeshData* meshData = TfMapLookupPtr(_skelMeshDataMap, primPath);
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
            }
            else
            {
                const _EntityPrimPropertyInfo* propInfo = TfMapLookupPtr(*_skinMeshEntityProperties, nameToken);
                if (propInfo != NULL)
                {
                    // Check that it belongs to a leaf prim before getting the type name value
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
                    if (entityData)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(propInfo->typeName);
                    }

                    return false;
                }
                else
                {
                    propInfo = TfMapLookupPtr(*_skinMeshProperties, nameToken);
                }
                if (propInfo != NULL)
                {
                    // Check that it belongs to a leaf prim before getting the type name value
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
                    if (meshData)
                    {
                        RETURN_TRUE_WITH_OPTIONAL_VALUE(propInfo->typeName);
                    }

                    return false;
                }
                    
                {
                    const SkinMeshEntityData* entityData = TfMapLookupPtr(_skinMeshEntityDataMap, primPath);
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
                
                {
                    const SkinMeshData* meshData = TfMapLookupPtr(_skinMeshDataMap, primPath);
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
            }

            return false;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeEntityMeshNames(glm::Array<glm::GlmString>& meshNames, glm::crowdio::OutputEntityGeoData& outputData, const SkinMeshEntityData* entityData) const
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
        void GolaemUSD_DataImpl::_ComputeEntityMeshNames(glm::Array<glm::GlmString>& meshNames, PODArray<bool>& activeMeshes, glm::Array<PODArray<int>>& meshShadingGroups, const SkelEntityData* entityData) const
        {
            meshNames.clear();
            activeMeshes.clear();
            meshShadingGroups.clear();

            glm::Array<glm::GlmString> activeMeshNames;
            const glm::Array<glm::PODArray<int>>& entityAssets = entityData->data.inputGeoData._cachedSimulation->getFinalEntityAssets(entityData->data.inputGeoData._frames.empty() ? 0 : entityData->data.inputGeoData._frames.front());
            glm::PODArray<int> furAssetIds;
            glm::PODArray<size_t> meshAssetNameIndices;
            glm::PODArray<int> meshAssetMaterialIndices;
            glm::crowdio::computeMeshNames(
                entityData->character,
                entityData->data.inputGeoData._entityId,
                entityAssets[entityData->data.inputGeoData._entityIndex],
                activeMeshNames,
                furAssetIds,
                meshAssetNameIndices,
                meshAssetMaterialIndices);

            meshNames.resize(entityData->character->_meshAssets.size());
            activeMeshes.resize(entityData->character->_meshAssets.size());
            meshShadingGroups.resize(entityData->character->_meshAssets.size());
            for (size_t iMeshAsset = 0, meshAssetCount = entityData->character->_meshAssets.size(); iMeshAsset < meshAssetCount; ++iMeshAsset)
            {
                const MeshAsset& meshAsset = entityData->character->_meshAssets[iMeshAsset];
                meshShadingGroups[iMeshAsset] = meshAsset._shadingGroups;
                const GlmString& meshName = meshAsset._name;
                meshNames[iMeshAsset] = meshName;
                activeMeshes[iMeshAsset] = glmFind(activeMeshNames.begin(), activeMeshNames.end(), meshName) != activeMeshNames.end();
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeSkelEntity(const SkelEntityData* entityData, double time) const
        {
            glm::ScopedLock<glm::SpinLock> entityComputeLock(entityData->data.entityComputeLock);
            if (entityData->data.computedTimeSample != time)
            {
                entityData->data.computedTimeSample = time;

                _ComputeEntity(&entityData->data, entityData->character, time);
                if (!entityData->data.enabled)
                {
                    return;
                }
            }
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeEntity(EntityVolatileData* entityData, const GolaemCharacter* character, double time) const
        {
            const glm::crowdio::GlmSimulationData* simuData = NULL;
            const glm::crowdio::GlmFrameData* frameData = NULL;
            const glm::ShaderAssetDataContainer* shaderDataContainer = NULL;
            {
                glm::ScopedLock<glm::SpinLock> cachedSimuLock(*entityData->cachedSimulationLock);
                simuData = entityData->inputGeoData._cachedSimulation->getFinalSimulationData();
                frameData = entityData->inputGeoData._cachedSimulation->getFinalFrameData(time, UINT32_MAX, true);
                shaderDataContainer = entityData->inputGeoData._cachedSimulation->getFinalShaderData(time, UINT32_MAX, true);
            }
            if (simuData == NULL || frameData == NULL)
            {
                _InvalidateEntity(entityData);
                return;
            }

            entityData->enabled = frameData->_entityEnabled[entityData->inputGeoData._entityIndex] == 1;
            if (!entityData->enabled)
            {
                _InvalidateEntity(entityData);
                return;
            }

            int intAttrCounter = 0;
            int floatAttrCounter = 0;
            int stringAttrCounter = 0;
            int vectorAttrCounter = 0;
            // compute shader data
            const glm::Array<glm::GlmString>& shaderData = shaderDataContainer->data[entityData->inputGeoData._entityIndex];
            glm::Vector3 vectValue;
            for (size_t iShaderAttr = 0, shaderAttrCount = character->_shaderAttributes.size(); iShaderAttr < shaderAttrCount; iShaderAttr++)
            {
                const glm::GlmString& attrValueStr = shaderData[iShaderAttr];
                const glm::ShaderAttribute& shaderAttr = character->_shaderAttributes[iShaderAttr];
                switch (shaderAttr._type)
                {
                case glm::ShaderAttributeType::INT:
                {
                    glm::fromString<int>(attrValueStr, entityData->intShaderAttrValues[intAttrCounter]);
                    ++intAttrCounter;
                }
                break;
                case glm::ShaderAttributeType::FLOAT:
                {
                    glm::fromString<float>(attrValueStr, entityData->floatShaderAttrValues[floatAttrCounter]);
                    ++floatAttrCounter;
                }
                break;
                case glm::ShaderAttributeType::STRING:
                {
                    entityData->stringShaderAttrValues[stringAttrCounter] = TfToken(attrValueStr.c_str());
                    ++stringAttrCounter;
                }
                break;
                case glm::ShaderAttributeType::VECTOR:
                {
                    glm::fromString(attrValueStr, vectValue);
                    entityData->vectorShaderAttrValues[vectorAttrCounter].Set(vectValue.getFloatValues());
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
                entityData->floatPPAttrValues[iFloatPPAttr] = frameData->_ppFloatAttributeData[iFloatPPAttr][entityData->inputGeoData._entityIndex];
            }
            for (uint8_t iVectPPAttr = 0; iVectPPAttr < simuData->_ppVectorAttributeCount; ++iVectPPAttr)
            {
                entityData->vectorPPAttrValues[iVectPPAttr].Set(frameData->_ppVectorAttributeData[iVectPPAttr][entityData->inputGeoData._entityIndex]);
            }

            // update frame before computing geometry
            entityData->inputGeoData._frames.resize(1);
            entityData->inputGeoData._frames[0] = time;
            entityData->inputGeoData._frameDatas.resize(1);
            entityData->inputGeoData._frameDatas[0] = frameData;
        }

        //-----------------------------------------------------------------------------
        void GolaemUSD_DataImpl::_ComputeSkinMeshEntity(const SkinMeshEntityData* entityData, double time) const
        {
            // check if computation is needed
            glm::ScopedLock<glm::SpinLock> entityComputeLock(entityData->data.entityComputeLock);
            if (entityData->data.computedTimeSample != time)
            {
                entityData->data.computedTimeSample = time;

                _ComputeEntity(&entityData->data, entityData->character, time);
                if (!entityData->data.enabled)
                {
                    return;
                }

                GolaemDisplayMode::Value displayMode = (GolaemDisplayMode::Value)_params.glmDisplayMode;
                // update entity position

                const glm::crowdio::GlmFrameData* frameData = entityData->data.inputGeoData._frameDatas[0];

                float* rootPos = frameData->_bonePositions[entityData->data.bonePositionOffset];
                entityData->data.pos.Set(rootPos);

                switch (displayMode)
                {
                case glm::usdplugin::GolaemDisplayMode::SKINMESH:
                {
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

                                const SkinMeshData* meshData = entityData->data.meshData[iRenderMesh];

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

                                const SkinMeshData* meshData = entityData->data.meshData[iRenderMesh];

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
        void GolaemUSD_DataImpl::_InvalidateEntity(EntityVolatileData* entityData) const
        {
            entityData->enabled = false;
            entityData->inputGeoData._frames.clear();
            entityData->inputGeoData._frameDatas.clear();
            entityData->intShaderAttrValues.clear();
            entityData->floatShaderAttrValues.clear();
            entityData->stringShaderAttrValues.clear();
            entityData->vectorShaderAttrValues.clear();
        }

    } // namespace usdplugin
} // namespace glm