#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

ly_get_list_relative_pal_filename(pal_editor_include_dir ${CMAKE_CURRENT_LIST_DIR}/Include/Private/Editor/Platform/${PAL_PLATFORM_NAME})
ly_get_list_relative_pal_filename(pal_cafile_include_dir ${CMAKE_CURRENT_LIST_DIR}/Source/Framework/Platform/${PAL_PLATFORM_NAME})

ly_add_target(
    NAME AWSCore.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE
        awscore_files.cmake
         ${pal_cafile_include_dir}/platform_${PAL_PLATFORM_NAME_LOWERCASE}_files.cmake
    INCLUDE_DIRECTORIES
        PUBLIC
            Include/Public
        PRIVATE
            Include/Private
    BUILD_DEPENDENCIES
        PRIVATE
            AZ::AzCore
            AZ::AzFramework
            AZ::AWSNativeSDKInit
        PUBLIC
            3rdParty::AWSNativeSDK::AWSCore
)

ly_add_target( 
    NAME AWSCore ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE}
    NAMESPACE Gem
    FILES_CMAKE
        awscore_shared_files.cmake
    INCLUDE_DIRECTORIES
        PRIVATE
            Include/Private
    BUILD_DEPENDENCIES
        PRIVATE
            AZ::AzCore
        PUBLIC
            Gem::AWSCore.Static
)

# clients and servers will use the above Gem::AWSCore module.
ly_create_alias(NAME AWSCore.Servers NAMESPACE Gem TARGETS Gem::AWSCore)
ly_create_alias(NAME AWSCore.Clients NAMESPACE Gem TARGETS Gem::AWSCore)

if (PAL_TRAIT_BUILD_HOST_TOOLS)
    ly_add_target(
        NAME AWSCore.Editor.Static STATIC
        NAMESPACE Gem
        FILES_CMAKE
            awscore_editor_files.cmake
            ${pal_editor_include_dir}/platform_${PAL_PLATFORM_NAME_LOWERCASE}_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                Include/Private
                ${pal_editor_include_dir}
            PUBLIC
                Include/Public
        BUILD_DEPENDENCIES
            PRIVATE
                AZ::AzQtComponents
                3rdParty::Qt::Core
                3rdParty::Qt::Widgets
                Gem::AWSCore.Static
            PUBLIC
                AZ::AzToolsFramework
                3rdParty::AWSNativeSDK::AWSCore
    )

    ly_add_target(
        NAME AWSCore.Editor GEM_MODULE
        NAMESPACE Gem
        FILES_CMAKE
            awscore_editor_shared_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                Include/Private
        BUILD_DEPENDENCIES
            PRIVATE
                AZ::AzCore
                Gem::AWSCore.Editor.Static
        RUNTIME_DEPENDENCIES
            Gem::AWSCore
    )

    # This target is not a real gem module
    # It is not meant to be loaded by the ModuleManager in C++
    ly_add_target(
        NAME AWSCore.ResourceMappingTool MODULE
        NAMESPACE Gem
        OUTPUT_SUBDIRECTORY AWSCoreEditorQtBin
        FILES_CMAKE
            awscore_resourcemappingtool_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                Include/Private
        BUILD_DEPENDENCIES
            PRIVATE
                Gem::AWSCore.Editor.Static
    )
    ly_add_dependencies(AWSCore.Editor AWSCore.ResourceMappingTool)

    # Builders and Tools (such as the Editor use AWSCore.Editor) use the .Editor module above.
    ly_create_alias(NAME AWSCore.Tools NAMESPACE Gem TARGETS Gem::AWSCore.Editor)
    ly_create_alias(NAME AWSCore.Builders NAMESPACE Gem TARGETS Gem::AWSCore.Editor)

endif()

################################################################################
# Tests
################################################################################
if(PAL_TRAIT_BUILD_TESTS_SUPPORTED)
    ly_add_target(
        NAME AWSCore.Tests MODULE
        NAMESPACE Gem
        FILES_CMAKE
            awscore_tests_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                Include/Private
                Include/Public
                Tests
        BUILD_DEPENDENCIES
            PRIVATE
                AZ::AzTest
                AZ::AzFramework
                AZ::AWSNativeSDKInit
                Gem::AWSCore.Static
    )
    ly_add_googletest(
        NAME Gem::AWSCore.Tests
    )

    if (PAL_TRAIT_BUILD_HOST_TOOLS)
        ly_add_target(
            NAME AWSCore.Editor.Tests ${PAL_TRAIT_TEST_TARGET_TYPE}
            NAMESPACE Gem
            FILES_CMAKE
                awscore_editor_tests_files.cmake
                ${pal_editor_include_dir}/platform_${PAL_PLATFORM_NAME_LOWERCASE}_files.cmake
                Tests/Editor/Platform/${PAL_PLATFORM_NAME}/awscore_editor_tests_${PAL_PLATFORM_NAME_LOWERCASE}_files.cmake
            INCLUDE_DIRECTORIES
                PRIVATE
                    Include/Private
                    ${pal_editor_include_dir}
                    Include/Public
                    Tests
            COMPILE_DEFINITIONS
                PRIVATE
                    AWSCORE_EDITOR
            BUILD_DEPENDENCIES
                PRIVATE
                    3rdParty::Qt::Gui
                    3rdParty::Qt::Widgets
                    AZ::AzTest
                    Gem::AWSCore.Static
                    Gem::AWSCore.Editor.Static
        )
        ly_add_googletest(
            NAME Gem::AWSCore.Editor.Tests
        )
    endif()
endif()

install(DIRECTORY "Tools/ResourceMappingTool"
    DESTINATION "Gems/AWSCore/Code/Tools"
    COMPONENT ${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME}
    PATTERN "__pycache__" EXCLUDE
)
