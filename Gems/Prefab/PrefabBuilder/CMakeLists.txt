#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

if(NOT PAL_TRAIT_BUILD_HOST_TOOLS)
    return()
endif()

ly_add_target(
    NAME PrefabBuilder.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE
        prefabbuilder_files.cmake
    BUILD_DEPENDENCIES
        PUBLIC
            AZ::AzCore
            AZ::AzToolsFramework
            AZ::AssetBuilderSDK
)

ly_add_target(
    NAME PrefabBuilder GEM_MODULE
    NAMESPACE Gem
    INCLUDE_DIRECTORIES
        PRIVATE
            .
    FILES_CMAKE
        prefabbuilder_module_files.cmake
    BUILD_DEPENDENCIES
        PRIVATE
            Gem::PrefabBuilder.Static
)

# the prefab builder only needs to be active in builders
# use the PrefabBuilder module in Clients and Servers:
ly_create_alias(NAME PrefabBuilder.Builders NAMESPACE Gem TARGETS Gem::PrefabBuilder)

# we automatically add this gem, if it is present, to all our known set of builder applications:
ly_enable_gems(GEMS PrefabBuilder VARIANTS Builders TARGETS AssetProcessor AssetProcessorBatch AssetBuilder)

# if you have a custom builder application in your project, then use ly_enable_gems() to
# add it to that application for your project, like this to make YOUR_TARGET_NAME load it automatically
# ly_enable_gems(PROJECT_NAME (YOUR_PROJECT_NAME) GEMS PrefabBuilder VARIANTS Builders TARGETS (YOUR_TARGET_NAME) )

if(PAL_TRAIT_BUILD_TESTS_SUPPORTED)
    ly_add_target(
        NAME PrefabBuilder.Tests ${PAL_TRAIT_TEST_TARGET_TYPE}
        NAMESPACE Gem
        FILES_CMAKE
            prefabbuilder_tests_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                .
        BUILD_DEPENDENCIES
            PRIVATE
                AZ::AzTest
                Gem::PrefabBuilder.Static
    )
    ly_add_googletest(
        NAME Gem::PrefabBuilder.Tests
    )
endif()
