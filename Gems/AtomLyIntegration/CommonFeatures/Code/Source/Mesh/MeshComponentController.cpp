/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Mesh/MeshComponentController.h>

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentConstants.h>

#include <Atom/Feature/Mesh/MeshFeatureProcessor.h>

#include <Atom/RPI.Public/Model/Model.h>
#include <Atom/RPI.Public/Scene.h>

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Debug/EventTrace.h>
#include <AzCore/Serialization/SerializeContext.h>

#include <AzFramework/Entity/EntityContextBus.h>
#include <AzFramework/Entity/EntityContext.h>
#include <AzFramework/Scene/Scene.h>
#include <AzFramework/Scene/SceneSystemInterface.h>

#include <AzCore/RTTI/BehaviorContext.h>

namespace AZ
{
    namespace Render
    {
        void MeshComponentConfig::Reflect(ReflectContext* context)
        {
            if (auto* serializeContext = azrtti_cast<SerializeContext*>(context))
            {
                serializeContext->Class<MeshComponentConfig>()
                    ->Version(1)
                    ->Field("ModelAsset", &MeshComponentConfig::m_modelAsset)
                    ->Field("SortKey", &MeshComponentConfig::m_sortKey)
                    ->Field("LodOverride", &MeshComponentConfig::m_lodOverride)
                    ->Field("ExcludeFromReflectionCubeMaps", &MeshComponentConfig::m_excludeFromReflectionCubeMaps)
                    ->Field("UseForwardPassIBLSpecular", &MeshComponentConfig::m_useForwardPassIblSpecular);
            }
        }

        bool MeshComponentConfig::IsAssetSet()
        {
            return m_modelAsset.GetId().IsValid();
        }

        AZStd::vector<AZStd::pair<RPI::Cullable::LodOverride, AZStd::string>> MeshComponentConfig::GetLodOverrideValues()
        {
            AZStd::vector<AZStd::pair<RPI::Cullable::LodOverride, AZStd::string>> values;
            uint32_t lodCount = 0;
            if (IsAssetSet())
            {
                if (m_modelAsset.IsReady())
                {
                    lodCount = static_cast<uint32_t>(m_modelAsset->GetLodCount());
                }
                else
                {
                    // If the asset isn't loaded, it's still possible it exists in the instance database.
                    Data::Instance<RPI::Model> model = Data::InstanceDatabase<RPI::Model>::Instance().Find(Data::InstanceId::CreateFromAssetId(m_modelAsset.GetId()));
                    if (model)
                    {
                        lodCount = static_cast<uint32_t>(model->GetLodCount());
                    }
                }
            }

            values.reserve(lodCount + 1);
            values.push_back({ RPI::Cullable::NoLodOverride, "Not Set" });

            for (uint32_t i = 0; i < lodCount; ++i)
            {
                AZStd::string enumDescription = AZStd::string::format("Lod %i", i);
                values.push_back({ aznumeric_cast<RPI::Cullable::LodOverride>(i), enumDescription.c_str() });
            }

            return values;
        }

        MeshComponentController::~MeshComponentController()
        {
            // Release memory, disconnect from buses in the right order and broadcast events so that other components are aware.
            Deactivate();
        }

        void MeshComponentController::Reflect(ReflectContext* context)
        {
            MeshComponentConfig::Reflect(context);

            if (auto* serializeContext = azrtti_cast<SerializeContext*>(context))
            {
                serializeContext->Class<MeshComponentController>()
                    ->Version(0)
                    ->Field("Configuration", &MeshComponentController::m_configuration);
            }

            if (AZ::BehaviorContext* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
            {
                behaviorContext->ConstantProperty("NoLodOverride", BehaviorConstant(RPI::Cullable::NoLodOverride))
                    ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                    ->Attribute(AZ::Script::Attributes::Category, "render")
                    ->Attribute(AZ::Script::Attributes::Module, "render");

                behaviorContext->EBus<MeshComponentRequestBus>("RenderMeshComponentRequestBus")
                    ->Event("GetModelAssetId", &MeshComponentRequestBus::Events::GetModelAssetId)
                    ->Event("SetModelAssetId", &MeshComponentRequestBus::Events::SetModelAssetId)
                    ->Event("GetModelAssetPath", &MeshComponentRequestBus::Events::GetModelAssetPath)
                    ->Event("SetModelAssetPath", &MeshComponentRequestBus::Events::SetModelAssetPath)
                    ->Event("SetSortKey", &MeshComponentRequestBus::Events::SetSortKey)
                    ->Event("GetSortKey", &MeshComponentRequestBus::Events::GetSortKey)
                    ->Event("SetLodOverride", &MeshComponentRequestBus::Events::SetLodOverride)
                    ->Event("GetLodOverride", &MeshComponentRequestBus::Events::GetLodOverride)
                    ->VirtualProperty("ModelAssetId", "GetModelAssetId", "SetModelAssetId")
                    ->VirtualProperty("ModelAssetPath", "GetModelAssetPath", "SetModelAssetPath")
                    ->VirtualProperty("SortKey", "GetSortKey", "SetSortKey")
                    ->VirtualProperty("LodOverride", "GetLodOverride", "SetLodOverride")
                    ;
            }
        }

        void MeshComponentController::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
        {
            dependent.push_back(AZ_CRC("TransformService", 0x8ee22c50));
            dependent.push_back(AZ_CRC_CE("NonUniformScaleService"));
        }

        void MeshComponentController::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(AZ_CRC("MaterialReceiverService", 0x0d1a6a74));
            provided.push_back(AZ_CRC("MeshService", 0x71d8a455));
        }

        void MeshComponentController::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
        {
            incompatible.push_back(AZ_CRC("MaterialReceiverService", 0x0d1a6a74));
            incompatible.push_back(AZ_CRC("MeshService", 0x71d8a455));
        }

        // [GFX TODO] [ATOM-13339] Remove the ModelAsset id fix up function in MeshComponentController
        // Model id was changed due to fix for [ATOM-13312]. We can remove this code when all the levels are updated.
        void FixUpModelAsset(Data::Asset<RPI::ModelAsset>& modelAsset)
        {
            Data::AssetId assetId;
            Data::AssetCatalogRequestBus::BroadcastResult(
                assetId,
                &Data::AssetCatalogRequestBus::Events::GetAssetIdByPath,
                modelAsset.GetHint().c_str(),
                AZ::RPI::ModelAsset::TYPEINFO_Uuid(),
                false);
            if (assetId != modelAsset.GetId())
            {
                if (assetId.IsValid())
                {
                    modelAsset = Data::Asset<RPI::ModelAsset>{ assetId, AZ::RPI::ModelAsset::TYPEINFO_Uuid(), modelAsset.GetHint().c_str() };
                    modelAsset.SetAutoLoadBehavior(AZ::Data::AssetLoadBehavior::QueueLoad);
                }
                else
                {
                    AZ_Error("MeshComponentController", false, "Failed to find asset id for [%s] ", modelAsset.GetHint().c_str());
                }
            }
        }

        MeshComponentController::MeshComponentController(const MeshComponentConfig& config)
            : m_configuration(config)
        {
            FixUpModelAsset(m_configuration.m_modelAsset);
        }

        void MeshComponentController::Activate(const AZ::EntityComponentIdPair& entityComponentIdPair)
        {
            FixUpModelAsset(m_configuration.m_modelAsset);

            const AZ::EntityId entityId = entityComponentIdPair.GetEntityId();
            m_entityComponentIdPair = entityComponentIdPair;

            m_transformInterface = TransformBus::FindFirstHandler(entityId);
            AZ_Warning("MeshComponentController", m_transformInterface, "Unable to attach to a TransformBus handler. This mesh will always be rendered at the origin.");

            m_meshFeatureProcessor = RPI::Scene::GetFeatureProcessorForEntity<MeshFeatureProcessorInterface>(entityId);
            AZ_Error("MeshComponentController", m_meshFeatureProcessor, "Unable to find a MeshFeatureProcessorInterface on the entityId.");

            m_cachedNonUniformScale = AZ::Vector3::CreateOne();
            AZ::NonUniformScaleRequestBus::EventResult(m_cachedNonUniformScale, entityId, &AZ::NonUniformScaleRequests::GetScale);
            AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent,
                m_nonUniformScaleChangedHandler);

            MeshComponentRequestBus::Handler::BusConnect(entityId);
            TransformNotificationBus::Handler::BusConnect(entityId);
            MaterialReceiverRequestBus::Handler::BusConnect(entityId);
            MaterialComponentNotificationBus::Handler::BusConnect(entityId);
            AzFramework::BoundsRequestBus::Handler::BusConnect(entityId);
            AzFramework::EntityContextId contextId;
            AzFramework::EntityIdContextQueryBus::EventResult(
                contextId, entityId, &AzFramework::EntityIdContextQueries::GetOwningContextId);
            AzFramework::RenderGeometry::IntersectionRequestBus::Handler::BusConnect({entityId, contextId});

            //Buses must be connected before RegisterModel in case requests are made as a result of HandleModelChange
            RegisterModel();
        }

        void MeshComponentController::Deactivate()
        {
            // Buses must be disconnected after unregistering the model, otherwise they can't deliver the events during the process.
            UnregisterModel();

            AzFramework::RenderGeometry::IntersectionRequestBus::Handler::BusDisconnect();
            AzFramework::BoundsRequestBus::Handler::BusDisconnect();
            MeshComponentRequestBus::Handler::BusDisconnect();
            TransformNotificationBus::Handler::BusDisconnect();
            MaterialReceiverRequestBus::Handler::BusDisconnect();
            MaterialComponentNotificationBus::Handler::BusDisconnect();

            m_nonUniformScaleChangedHandler.Disconnect();

            m_meshFeatureProcessor = nullptr;
            m_transformInterface = nullptr;
            m_entityComponentIdPair = AZ::EntityComponentIdPair(AZ::EntityId(), AZ::InvalidComponentId);
            m_configuration.m_modelAsset.Release();
        }

        void MeshComponentController::SetConfiguration(const MeshComponentConfig& config)
        {
            m_configuration = config;
        }

        const MeshComponentConfig& MeshComponentController::GetConfiguration() const
        {
            return m_configuration;
        }

        void MeshComponentController::OnTransformChanged(const AZ::Transform& /*local*/, const AZ::Transform& world)
        {
            if (m_meshFeatureProcessor)
            {
                m_meshFeatureProcessor->SetTransform(m_meshHandle, world, m_cachedNonUniformScale);
            }
        }

        void MeshComponentController::HandleNonUniformScaleChange(const AZ::Vector3& nonUniformScale)
        {
            m_cachedNonUniformScale = nonUniformScale;
            if (m_meshFeatureProcessor)
            {
                m_meshFeatureProcessor->SetTransform(m_meshHandle, m_transformInterface->GetWorldTM(), m_cachedNonUniformScale);
            }
        }
        
        RPI::ModelMaterialSlotMap MeshComponentController::GetModelMaterialSlots() const
        {
            Data::Asset<const RPI::ModelAsset> modelAsset = GetModelAsset();
            if (modelAsset.IsReady())
            {
                return modelAsset->GetMaterialSlots();
            }
            else
            {
                return {};
            }
        }

        MaterialAssignmentId MeshComponentController::FindMaterialAssignmentId(
            const MaterialAssignmentLodIndex lod, const AZStd::string& label) const
        {
            return FindMaterialAssignmentIdInModel(GetModel(), lod, label);
        }

        MaterialAssignmentMap MeshComponentController::GetMaterialAssignments() const
        {
            return GetMaterialAssignmentsFromModel(GetModel());
        }

        AZStd::unordered_set<AZ::Name> MeshComponentController::GetModelUvNames() const
        {
            const Data::Instance<RPI::Model> model = GetModel();
            return model ? model->GetUvNames() : AZStd::unordered_set<AZ::Name>();
        }

        void MeshComponentController::OnMaterialsUpdated([[maybe_unused]] const MaterialAssignmentMap& materials)
        {
            if (m_meshFeatureProcessor)
            {
                m_meshFeatureProcessor->SetMaterialAssignmentMap(m_meshHandle, materials);
            }
        }

        bool MeshComponentController::RequiresCloning(const Data::Asset<RPI::ModelAsset>& modelAsset)
        {
            // Is the model asset containing a cloth buffer? If yes, we need to clone the model asset for instancing.
            const AZStd::array_view<AZ::Data::Asset<AZ::RPI::ModelLodAsset>> lodAssets = modelAsset->GetLodAssets();
            for (const AZ::Data::Asset<AZ::RPI::ModelLodAsset>& lodAsset : lodAssets)
            {
                const AZStd::array_view<AZ::RPI::ModelLodAsset::Mesh> meshes = lodAsset->GetMeshes();
                for (const AZ::RPI::ModelLodAsset::Mesh& mesh : meshes)
                {
                    if (mesh.GetSemanticBufferAssetView(AZ::Name("CLOTH_DATA")) != nullptr)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        void MeshComponentController::HandleModelChange(Data::Instance<RPI::Model> model)
        {
            Data::Asset<RPI::ModelAsset> modelAsset = m_meshFeatureProcessor->GetModelAsset(m_meshHandle);
            if (model && modelAsset)
            {
                const AZ::EntityId entityId = m_entityComponentIdPair.GetEntityId();
                m_configuration.m_modelAsset = modelAsset;
                MeshComponentNotificationBus::Event(entityId, &MeshComponentNotificationBus::Events::OnModelReady, m_configuration.m_modelAsset, model);
                MaterialReceiverNotificationBus::Event(entityId, &MaterialReceiverNotificationBus::Events::OnMaterialAssignmentsChanged);
                AZ::Interface<AzFramework::IEntityBoundsUnion>::Get()->RefreshEntityLocalBoundsUnion(entityId);
            }
        }

        void MeshComponentController::RegisterModel()
        {
            if (m_meshFeatureProcessor && m_configuration.m_modelAsset.GetId().IsValid())
            {
                const AZ::EntityId entityId = m_entityComponentIdPair.GetEntityId();

                MaterialAssignmentMap materials;
                MaterialComponentRequestBus::EventResult(materials, entityId, &MaterialComponentRequests::GetMaterialOverrides);

                m_meshFeatureProcessor->ReleaseMesh(m_meshHandle);
                MeshHandleDescriptor meshDescriptor;
                meshDescriptor.m_modelAsset = m_configuration.m_modelAsset;
                meshDescriptor.m_useForwardPassIblSpecular = m_configuration.m_useForwardPassIblSpecular;
                meshDescriptor.m_requiresCloneCallback = RequiresCloning;
                m_meshHandle = m_meshFeatureProcessor->AcquireMesh(meshDescriptor, materials);
                m_meshFeatureProcessor->ConnectModelChangeEventHandler(m_meshHandle, m_changeEventHandler);

                const AZ::Transform& transform = m_transformInterface ? m_transformInterface->GetWorldTM() : AZ::Transform::CreateIdentity();

                m_meshFeatureProcessor->SetTransform(m_meshHandle, transform, m_cachedNonUniformScale);
                m_meshFeatureProcessor->SetSortKey(m_meshHandle, m_configuration.m_sortKey);
                m_meshFeatureProcessor->SetLodOverride(m_meshHandle, m_configuration.m_lodOverride);
                m_meshFeatureProcessor->SetExcludeFromReflectionCubeMaps(m_meshHandle, m_configuration.m_excludeFromReflectionCubeMaps);
                m_meshFeatureProcessor->SetVisible(m_meshHandle, m_isVisible);

                // [GFX TODO] This should happen automatically. m_changeEventHandler should be passed to AcquireMesh
                // If the model instance or asset already exists, announce a model change to let others know it's loaded.
                HandleModelChange(m_meshFeatureProcessor->GetModel(m_meshHandle));
            }
        }

        void MeshComponentController::UnregisterModel()
        {
            if (m_meshFeatureProcessor && m_meshHandle.IsValid())
            {
                MeshComponentNotificationBus::Event(
                    m_entityComponentIdPair.GetEntityId(), &MeshComponentNotificationBus::Events::OnModelPreDestroy);
                m_meshFeatureProcessor->ReleaseMesh(m_meshHandle);
            }
        }

        void MeshComponentController::RefreshModelRegistration()
        {
            // [GFX TODO][ATOM-13364] Without the Suspend / Resume calls below, a model refresh will trigger an asset unload and reload
            // that breaks Material Thumbnail Previews in the Editor.  The asset unload/reload itself is undesirable, but the flow should
            // get investigated further to determine what state management and notifications need to be modified, since the previews ought
            // to still work even if a full asset reload were to occur here.

            // The unregister / register combination can cause the asset reference to get released, which could trigger a full reload
            // of the asset.  Tell the Asset Manager not to release any asset references until after the registration is complete.
            // This will ensure that if we're reusing the same model, it remains loaded.
            Data::AssetManager::Instance().SuspendAssetRelease();
            UnregisterModel();
            RegisterModel();
            Data::AssetManager::Instance().ResumeAssetRelease();
        }

        void MeshComponentController::SetModelAssetId(Data::AssetId modelAssetId)
        {
            SetModelAsset(Data::Asset<RPI::ModelAsset>(modelAssetId, azrtti_typeid<RPI::ModelAsset>()));
        }

        void MeshComponentController::SetModelAsset(Data::Asset<RPI::ModelAsset> modelAsset)
        {
            if (m_configuration.m_modelAsset != modelAsset)
            {
                m_configuration.m_modelAsset = modelAsset;
                m_configuration.m_modelAsset.SetAutoLoadBehavior(Data::AssetLoadBehavior::PreLoad);
                RefreshModelRegistration();
            }
        }

        Data::Asset<const RPI::ModelAsset> MeshComponentController::GetModelAsset() const
        {
            return m_configuration.m_modelAsset;
        }

        Data::AssetId MeshComponentController::GetModelAssetId() const
        {
            return m_configuration.m_modelAsset.GetId();
        }

        void MeshComponentController::SetModelAssetPath(const AZStd::string& modelAssetPath)
        {
            AZ::Data::AssetId assetId;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(assetId, &AZ::Data::AssetCatalogRequestBus::Events::GetAssetIdByPath, modelAssetPath.c_str(), AZ::RPI::ModelAsset::RTTI_Type(), false);
            SetModelAssetId(assetId);
        }

        AZStd::string MeshComponentController::GetModelAssetPath() const
        {
            AZStd::string assetPathString;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(assetPathString, &AZ::Data::AssetCatalogRequests::GetAssetPathById, m_configuration.m_modelAsset.GetId());
            return assetPathString;
        }

        Data::Instance<RPI::Model> MeshComponentController::GetModel() const
        {
            return m_meshFeatureProcessor ? m_meshFeatureProcessor->GetModel(m_meshHandle) : Data::Instance<RPI::Model>();
        }

        void MeshComponentController::SetSortKey(RHI::DrawItemSortKey sortKey)
        {
            m_configuration.m_sortKey = sortKey; // Save for serialization
            m_meshFeatureProcessor->SetSortKey(m_meshHandle, sortKey);
        }

        RHI::DrawItemSortKey MeshComponentController::GetSortKey() const
        {
            return m_meshFeatureProcessor->GetSortKey(m_meshHandle);
        }

        void MeshComponentController::SetLodOverride(RPI::Cullable::LodOverride lodOverride)
        {
            m_configuration.m_lodOverride = lodOverride; // Save for serialization
            m_meshFeatureProcessor->SetLodOverride(m_meshHandle, lodOverride);
        }

        RPI::Cullable::LodOverride MeshComponentController::GetLodOverride() const
        {
            return static_cast<RPI::Cullable::LodOverride>(m_meshFeatureProcessor->GetSortKey(m_meshHandle));
        }

        void MeshComponentController::SetVisibility(bool visible)
        {
            if (m_isVisible != visible)
            {
                if (m_meshFeatureProcessor)
                {
                    m_meshFeatureProcessor->SetVisible(m_meshHandle, visible);
                }
                m_isVisible = visible;
            }
        }

        bool MeshComponentController::GetVisibility() const
        {
            return m_isVisible;
        }

        Aabb MeshComponentController::GetWorldBounds()
        {
            if (const AZ::Aabb localBounds = GetLocalBounds(); localBounds.IsValid())
            {
                return localBounds.GetTransformedAabb(m_transformInterface->GetWorldTM());
            }

            return AZ::Aabb::CreateNull();
        }

        Aabb MeshComponentController::GetLocalBounds()
        {
            if (m_meshHandle.IsValid() && m_meshFeatureProcessor)
            {
                Aabb aabb = m_meshFeatureProcessor->GetLocalAabb(m_meshHandle);

                aabb.MultiplyByScale(m_cachedNonUniformScale);
                return aabb;
            }
            else
            {
                return Aabb::CreateNull();
            }
        }

        AzFramework::RenderGeometry::RayResult MeshComponentController::RenderGeometryIntersect(
            const AzFramework::RenderGeometry::RayRequest& ray)
        {
            AzFramework::RenderGeometry::RayResult result;
            if (const Data::Instance<RPI::Model> model = GetModel())
            {
                float t;
                AZ::Vector3 normal;
                if (model->RayIntersection(
                        m_transformInterface->GetWorldTM(), m_cachedNonUniformScale, ray.m_startWorldPosition,
                        ray.m_endWorldPosition - ray.m_startWorldPosition, t, normal))
                {
                    // fill in ray result structure after successful intersection
                    const auto intersectionLine = (ray.m_endWorldPosition - ray.m_startWorldPosition);
                    result.m_uv = AZ::Vector2::CreateZero();
                    result.m_worldPosition = ray.m_startWorldPosition + intersectionLine * t;
                    result.m_worldNormal = normal;
                    result.m_distance = intersectionLine.GetLength() * t;
                    result.m_entityAndComponent = m_entityComponentIdPair;
                }
            }

            return result;
        }
    } // namespace Render
} // namespace AZ
