/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/string/string.h>
#include <AzCore/std/containers/vector.h>

#include <AzCore/Asset/AssetCommon.h>
#include <Atom/RPI.Reflect/Material/MaterialAsset.h>
#include <Atom/Feature/Material/MaterialAssignment.h>

namespace AZ
{
    namespace Render
    {
        static const size_t DefaultMaterialSlotIndex = std::numeric_limits<size_t>::max();

        //! Details for a single editable material assignment
        struct EditorMaterialComponentSlot final
        {
            AZ_RTTI(EditorMaterialComponentSlot, "{344066EB-7C3D-4E92-B53D-3C9EBD546488}");
            AZ_CLASS_ALLOCATOR(EditorMaterialComponentSlot, SystemAllocator, 0);

            static void Reflect(ReflectContext* context);
            static bool ConvertVersion(AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement);

            AZ::Data::AssetId GetDefaultAssetId() const;
            AZStd::string GetLabel() const;
            bool HasSourceData() const;
            void OpenMaterialEditor() const;
            void ResetToDefaultAsset();
            void Clear();
            void ClearOverrides();
            void OpenMaterialExporter();
            void OpenMaterialInspector();
            void OpenUvNameMapInspector();

            MaterialAssignmentId m_id;
            AZStd::string m_label;
            Data::Asset<RPI::MaterialAsset> m_materialAsset;
            Data::Asset<RPI::MaterialAsset> m_defaultMaterialAsset;
            MaterialPropertyOverrideMap m_propertyOverrides;
            AZStd::function<void()> m_materialChangedCallback;
            AZStd::function<void()> m_propertyChangedCallback;

            RPI::MaterialModelUvOverrideMap m_matModUvOverrides;
            AZStd::unordered_set<AZ::Name> m_modelUvNames; // Cached for override options.

        private:
            void OpenPopupMenu(const AZ::Data::AssetId& assetId, const AZ::Data::AssetType& assetType);
            void OnMaterialChanged() const;
            void OnPropertyChanged() const;
        };

        // Vector of slots for assignable or overridable material data.
        using EditorMaterialComponentSlotContainer = AZStd::vector<EditorMaterialComponentSlot>;

        // Table containing all editable material data that is displayed in the edit context and inspector
        // The vector represents all the LODs that can have material overrides.
        // The container will be populated with every potential material slot on an associated model, using its default values.
        // Whenever changes are made to this container, the modified values are copied into the controller configuration material assignment map
        // as overrides that will be applied to material instances
        using EditorMaterialComponentSlotsByLodContainer = AZStd::vector<EditorMaterialComponentSlotContainer>;
    } // namespace Render
} // namespace AZ
