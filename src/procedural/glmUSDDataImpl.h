/***************************************************************************
*                                                                          *
*  Copyright (C) Golaem S.A.  All Rights Reserved.                         *
*                                                                          *
***************************************************************************/

#pragma once

#include "glmUSD.h"
#include "glmUSDData.h"

#include <glmSimulationCacheFactory.h>

namespace glm
{
    namespace usdplugin
    {
        using namespace PXR_INTERNAL_NS;

        struct GolaemDisplayMode
        {
            enum Value
            {
                BOUNDING_BOX,
                SKELETON,
                SKINMESH,
                END
            };
        };

        class GolaemUSD_DataImpl
        {
        private:
            // The parameters use to generate specs and time samples, obtained from the
            // layer's file format arguments.
            GolaemUSD_DataParams _params;

            crowdio::SimulationCacheFactory _factory;

            int _startFrame;
            int _endFrame;

            // Cached set of generated time sample times. All of the animated property
            // time sample fields have the same time sample times.
            std::set<double> _animTimeSampleTimes;

            // Cached set of all paths with a generated prim spec.
            TfHashSet<SdfPath, SdfPath::Hash> _primSpecPaths;

            // Cached list of the names of all child prims for each generated prim spec
            // that is not a leaf. The child prim names are the same for all prims that
            // make up the cube layout hierarchy.
            TfHashMap<SdfPath, std::vector<TfToken>, SdfPath::Hash> _primChildNames;

            struct EntityMeshData;
            struct EntityVolatileData
            {
                bool firstCompute = true;
                double computedTimeSample = 0;
                bool excluded = false; // excluded by layout - the entity will always be empty
                bool enabled = true; // can vary during simulation (kill, emit)
                GfVec3f pos{0, 0, 0};
                uint32_t bonePositionOffset = 0;
                glm::crowdio::InputEntityGeoData inputGeoData;
                glm::Array<EntityMeshData*> meshData;
                glm::SpinLock* cachedSimulationLock;
                glm::SpinLock _entityComputeLock; // do not allow simultaneous computes of the same entity
            };

            // cached data for each entity
            struct EntityData
            {
                mutable EntityVolatileData data;
                const glm::GolaemCharacter* character = NULL;
                TfHashMap<SdfPath, size_t, SdfPath::Hash> meshIds;
            };
            TfHashMap<SdfPath, EntityData, SdfPath::Hash> _entityDataMap;

            struct EntityMeshVolatileData
            {
                VtIntArray faceVertexCounts;
                VtIntArray faceVertexIndices;
                VtVec3fArray points;
                VtVec3fArray normals; // stored by polygon vertex
                SdfPath materialPath;
            };
            struct EntityMeshData
            {
                const EntityData* entityData = NULL;
                mutable EntityMeshVolatileData data;
            };
            TfHashMap<SdfPath, EntityMeshData, SdfPath::Hash> _entityMeshDataMap;

            mutable glm::GlmMap<const glm::crowdio::CachedSimulation*, glm::SpinLock> _cachedSimulationLocks;

        public:
            GolaemUSD_DataImpl(const GolaemUSD_DataParams& params);
            ~GolaemUSD_DataImpl();

            /// Returns true if the parameters produce no specs
            bool IsEmpty() const;

            /// Generates the spec type for the path.
            SdfSpecType GetSpecType(const SdfPath& path) const;

            /// Returns whether a value should exist for the given \a path and
            /// \a fieldName. Optionally returns the value if it exists.
            bool Has(const SdfPath& path, const TfToken& field, VtValue* value = NULL) const;

            /// Visits every spec generated from our params with the given
            /// \p visitor.
            void VisitSpecs(const SdfAbstractData& data, SdfAbstractDataSpecVisitor* visitor) const;

            /// Returns the list of all fields generated for spec path.
            const std::vector<TfToken>& List(const SdfPath& path) const;

            /// Returns a set that enumerates all integer frame values from 0 to the
            /// total number of animation frames specified in the params object.
            const std::set<double>& ListAllTimeSamples() const;

            /// Returns the same set as ListAllTimeSamples if the spec path is for one
            /// of the animated properties. Returns an empty set for all other spec
            /// paths.
            const std::set<double>& ListTimeSamplesForPath(const SdfPath& path) const;

            /// Returns the total number of animation frames if the spec path is for
            /// one of the animated properties. Returns 0 for all other spec paths.
            size_t GetNumTimeSamplesForPath(const SdfPath& path) const;

            /// Sets the upper and lower bound time samples of the value time and
            /// returns true as long as there are any animated frames for this data.
            bool GetBracketingTimeSamples(double time, double* tLower, double* tUpper) const;

            /// Sets the upper and lower bound time samples of the value time and
            /// returns true if the spec path is for one of the animated properties.
            /// Returns false for all other spec paths.
            bool GetBracketingTimeSamplesForPath(const SdfPath& path, double time, double* tLower, double* tUpper) const;

            /// Computes the value for the time sample if the spec path is one of the
            /// animated properties.
            bool QueryTimeSample(const SdfPath& path, double time, VtValue* value) const;

        private:
            // Initializes the cached data from the params object.
            void _InitFromParams();

            // Helper functions for queries about property specs.
            bool _IsAnimatedProperty(const SdfPath& path) const;
            bool _HasPropertyDefaultValue(const SdfPath& path, VtValue* value) const;
            bool _HasTargetPathValue(const SdfPath& path, VtValue* value) const;
            bool _HasPropertyTypeNameValue(const SdfPath& path, VtValue* value) const;
            bool _HasPropertyInterpolation(const SdfPath& path, VtValue* value) const;

            void _ComputeEntityMeshNames(glm::Array<glm::GlmString>& meshNames, const EntityData* entityData) const;
            void _ComputeEntity(const EntityData* entityData, double time) const;
            void _InvalidateEntity(const EntityData* entityData) const;
        };
    } // namespace usdplugin
} // namespace glm
