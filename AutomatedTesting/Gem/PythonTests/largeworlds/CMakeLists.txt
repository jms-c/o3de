#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

if(PAL_TRAIT_BUILD_TESTS_SUPPORTED AND PAL_TRAIT_BUILD_HOST_TOOLS AND PAL_TRAIT_LARGE_WORLDS_TEST_SUPPORTED)

## DynVeg ##

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationTests_Main
        TEST_SERIAL
        TEST_SUITE main
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "not SUITE_sandbox and not SUITE_periodic and not SUITE_benchmark"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.GameLauncher
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )


    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationTests_Sandbox
        TEST_SERIAL
        TEST_SUITE sandbox
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_sandbox"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.GameLauncher
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationFilterTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_filter"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationModifierTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_modifier"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationRegressionTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_regression"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationAreaTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_area"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationMiscTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_misc"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::DynamicVegetationSurfaceTagTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/dyn_veg
        PYTEST_MARKS "SUITE_periodic and dynveg_surfacetagemitter"
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )
## LandscapeCanvas ##

    ly_add_pytest(
        NAME AutomatedTesting::LandscapeCanvasTests_Main
        TEST_SERIAL
        TEST_SUITE main
        PATH ${CMAKE_CURRENT_LIST_DIR}/landscape_canvas/test_LandscapeCanvas_Main.py
        RUNTIME_DEPENDENCIES
           AZ::AssetProcessor
           Legacy::Editor
           AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

    ly_add_pytest(
        NAME AutomatedTesting::LandscapeCanvasTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/landscape_canvas/test_LandscapeCanvas_Periodic.py
        RUNTIME_DEPENDENCIES
           AZ::AssetProcessor
           Legacy::Editor
           AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

## GradientSignal ##
    ly_add_pytest(
        NAME AutomatedTesting::GradientSignalTests_Periodic
        TEST_SERIAL
        TEST_SUITE periodic
        PATH ${CMAKE_CURRENT_LIST_DIR}/gradient_signal/test_GradientSignal_Periodic.py
        RUNTIME_DEPENDENCIES
            AZ::AssetProcessor
            Legacy::Editor
            AutomatedTesting.Assets
        COMPONENT
            LargeWorlds
    )

endif()
