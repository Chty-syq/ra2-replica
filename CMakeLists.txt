cmake_minimum_required(VERSION 3.21)
project(ra2_replica_demo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(RA2_BUILD_DEMOS "Build the SDL/OpenGL demo and rendering experiments" ON)
option(RA2_ENABLE_GRAPHICS_TESTS "Run a hidden-window startup test (requires OpenGL 3.3)" OFF)
option(RA2_BUILD_BENCHMARKS "Build the reproducible headless simulation benchmark" OFF)

# Keep project warnings separate from vendored dependencies.
add_library(ra2_project_options INTERFACE)
if(MSVC)
  target_compile_options(ra2_project_options INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(ra2_project_options INTERFACE
    -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Decoders and pathfinding can be built and tested without SDL or OpenGL.
add_library(ra2_core STATIC
  src/config/ini_document.cpp
  src/config/definitions.cpp
  src/foundation.cpp
  src/art_ini.cpp
  src/infantry_art.cpp
  src/map_grid.cpp
  src/palette.cpp
  src/pathfinding.cpp
  src/rules_colors.cpp
  src/resource_art.cpp
  src/shp_ts.cpp
  src/tmp_tile.cpp
  src/vpl_file.cpp
  src/vxl_file.cpp
)
target_include_directories(ra2_core PUBLIC src)
target_link_libraries(ra2_core PRIVATE ra2_project_options)

add_library(ra2_entt INTERFACE)
target_include_directories(ra2_entt SYSTEM INTERFACE third_party/entt/include)
add_library(ra2_simulation STATIC src/simulation/world.cpp src/simulation/aircraft.cpp src/simulation/naval_aircraft.cpp src/simulation/flight.cpp src/simulation/radiation.cpp src/simulation/missile.cpp src/simulation/leap.cpp src/simulation/game_clock.cpp src/simulation/harvesting.cpp src/simulation/kinematics.cpp src/simulation/traffic.cpp)
target_link_libraries(ra2_simulation PUBLIC ra2_core PRIVATE ra2_entt ra2_project_options)

if(RA2_BUILD_BENCHMARKS)
  add_executable(ra2_calibration_probe tools/calibration_probe.cpp)
  target_link_libraries(ra2_calibration_probe PRIVATE ra2_simulation ra2_project_options)
  add_executable(ra2_simulation_benchmark tools/simulation_benchmark.cpp)
  target_link_libraries(ra2_simulation_benchmark PRIVATE ra2_simulation ra2_project_options)
endif()

if(BUILD_TESTING)
  add_executable(ra2_aircraft_tests tests/aircraft_tests.cpp)
  target_link_libraries(ra2_aircraft_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME aircraft_regressions COMMAND ra2_aircraft_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(aircraft_regressions PROPERTIES TIMEOUT 90)
  add_executable(ra2_naval_special_tests tests/naval_special_tests.cpp)
  target_link_libraries(ra2_naval_special_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME naval_special_regressions COMMAND ra2_naval_special_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(naval_special_regressions PROPERTIES TIMEOUT 90)
  add_executable(ra2_naval_tests tests/naval_tests.cpp)
  target_link_libraries(ra2_naval_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME naval_regressions COMMAND ra2_naval_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(naval_regressions PROPERTIES TIMEOUT 90)
  add_executable(ra2_commando_tests tests/commando_tests.cpp)
  target_link_libraries(ra2_commando_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME commando_regressions COMMAND ra2_commando_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(commando_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_ivan_apocalypse_tests tests/ivan_apocalypse_tests.cpp)
  target_link_libraries(ra2_ivan_apocalypse_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME ivan_apocalypse_regressions COMMAND ra2_ivan_apocalypse_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(ivan_apocalypse_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_demolition_tests tests/demolition_tests.cpp)
  target_link_libraries(ra2_demolition_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME demolition_regressions COMMAND ra2_demolition_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(demolition_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_projectile_impact_tests tests/projectile_impact_tests.cpp)
  target_link_libraries(ra2_projectile_impact_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME projectile_impact_regressions COMMAND ra2_projectile_impact_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(projectile_impact_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_dog_drone_tests tests/dog_drone_tests.cpp)
  target_link_libraries(ra2_dog_drone_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME dog_drone_regressions COMMAND ra2_dog_drone_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(dog_drone_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_radiation_v3_tests tests/radiation_v3_tests.cpp)
  target_link_libraries(ra2_radiation_v3_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME radiation_v3_regressions COMMAND ra2_radiation_v3_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(radiation_v3_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_tesla_unit_tests tests/tesla_unit_tests.cpp)
  target_link_libraries(ra2_tesla_unit_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME tesla_unit_regressions COMMAND ra2_tesla_unit_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(tesla_unit_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_special_vehicle_tests tests/special_vehicle_tests.cpp)
  target_link_libraries(ra2_special_vehicle_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME special_vehicle_regressions COMMAND ra2_special_vehicle_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(special_vehicle_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_air_defense_tests tests/air_defense_tests.cpp)
  target_link_libraries(ra2_air_defense_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME air_defense_regressions COMMAND ra2_air_defense_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(air_defense_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_transport_tests tests/transport_tests.cpp)
  target_link_libraries(ra2_transport_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME transport_regressions COMMAND ra2_transport_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(transport_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_support_infantry_tests tests/support_infantry_tests.cpp)
  target_link_libraries(ra2_support_infantry_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME support_infantry_regressions COMMAND ra2_support_infantry_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(support_infantry_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_infantry_tests tests/infantry_tests.cpp)
  target_link_libraries(ra2_infantry_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME infantry_regressions COMMAND ra2_infantry_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(infantry_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_traffic_tests tests/traffic_tests.cpp)
  target_link_libraries(ra2_traffic_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME cooperative_traffic_regressions COMMAND ra2_traffic_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(cooperative_traffic_regressions PROPERTIES TIMEOUT 120)
  add_executable(ra2_resource_art_tests tests/resource_art_tests.cpp)
  target_link_libraries(ra2_resource_art_tests PRIVATE ra2_core ra2_project_options)
  add_test(NAME resource_art_regressions COMMAND ra2_resource_art_tests "${PROJECT_SOURCE_DIR}/assets")
  set_tests_properties(resource_art_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_harvesting_tests tests/harvesting_tests.cpp)
  target_link_libraries(ra2_harvesting_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME harvesting_regressions COMMAND ra2_harvesting_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(harvesting_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_calibration_tests tests/calibration_tests.cpp)
  target_link_libraries(ra2_calibration_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME rule_calibration_regressions COMMAND ra2_calibration_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(rule_calibration_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_game_speed_tests tests/game_speed_tests.cpp)
  target_link_libraries(ra2_game_speed_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME game_speed_regressions COMMAND ra2_game_speed_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(game_speed_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_factory_tests tests/factory_tests.cpp)
  target_link_libraries(ra2_factory_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME factory_egress_regressions COMMAND ra2_factory_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(factory_egress_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_simulation_tests tests/simulation_tests.cpp)
  target_link_libraries(ra2_simulation_tests PRIVATE ra2_simulation ra2_project_options)
  add_test(NAME simulation_regressions COMMAND ra2_simulation_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(simulation_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_core_tests tests/core_tests.cpp)
  target_link_libraries(ra2_core_tests PRIVATE ra2_core ra2_project_options)
  add_test(NAME asset_decoder_regressions COMMAND ra2_core_tests parsers)
  add_test(NAME pathfinding_regressions COMMAND ra2_core_tests pathfinding)
  add_test(NAME project_asset_decode COMMAND ra2_core_tests assets "${PROJECT_SOURCE_DIR}/assets")
  set_tests_properties(asset_decoder_regressions pathfinding_regressions project_asset_decode
    PROPERTIES TIMEOUT 60)
endif()

if(NOT RA2_BUILD_DEMOS)
  return()
endif()

find_package(OpenGL REQUIRED)
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
  message(FATAL_ERROR "SDL2 source is missing. Please place it under third_party/SDL.")
endif()
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

# Expose the standalone tutorial as a regular executable target in IDEs.
# Its sources and dependencies are defined entirely in the experiment directory.
add_subdirectory(experiment/01-setup)
add_subdirectory(experiment/02-static-resource-rendering)

add_library(ra2_ui_backend STATIC
  third_party/glad/src/glad.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(ra2_ui_backend SYSTEM PUBLIC
  third_party/glad/include
  third_party/imgui
  third_party/imgui/backends
)
target_link_libraries(ra2_ui_backend PUBLIC OpenGL::GL SDL2::SDL2)

# Share implementations between the demo, experiments, and gameplay tests.
add_library(ra2_engine STATIC
  src/interaction/unit_selection.cpp
  src/presentation/world_view.cpp
  src/presentation/building_action_view.cpp
  src/presentation/naval_view.cpp
  src/presentation/infantry_view.cpp
  src/presentation/resource_view.cpp
  src/presentation/special_vehicle_view.cpp
  src/building_system_runtime.cpp
  src/demo_app_support.cpp
  src/demo_scene.cpp
  src/gl_loader.cpp
  src/imgui_debug_panel.cpp
  src/iso_grid_renderer.cpp
  src/iso_world.cpp
  src/renderer2d.cpp
  src/rhino_unit.cpp
  src/rhino_render_state.cpp
  src/sidebar.cpp
  src/vpl_box_renderer.cpp
  src/voxel_unit.cpp
  src/weapon_system.cpp
)
target_link_libraries(ra2_engine PUBLIC ra2_core ra2_simulation ra2_ui_backend PRIVATE ra2_project_options)

function(ra2_copy_runtime_dlls target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DRA2_RUNTIME_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:${target}>,|>"
        "-DRA2_RUNTIME_DIR=$<TARGET_FILE_DIR:${target}>"
        -P "${PROJECT_SOURCE_DIR}/cmake/CopyRuntimeDlls.cmake"
      VERBATIM
    )
  endif()
endfunction()

function(ra2_configure_sdl_app target)
  target_link_libraries(${target} PRIVATE ra2_project_options)
  if(MINGW)
    target_link_libraries(${target} PRIVATE mingw32)
  endif()
  target_link_libraries(${target} PRIVATE SDL2::SDL2main SDL2::SDL2)
  ra2_copy_runtime_dlls(${target})
endfunction()

add_executable(ra2_power_demo src/app_main_ui.cpp)
ra2_configure_sdl_app(ra2_power_demo)
target_link_libraries(ra2_power_demo PRIVATE ra2_engine)

add_executable(vxl_render_experiment experiment/vxl-render/src/main.cpp)
ra2_configure_sdl_app(vxl_render_experiment)
target_link_libraries(vxl_render_experiment PRIVATE ra2_engine)
target_compile_definitions(vxl_render_experiment PRIVATE
  VXL_RENDER_EXPERIMENT_ROOT="${PROJECT_SOURCE_DIR}/experiment/vxl-render"
)

add_executable(shp_render_experiment experiment/shp-render/src/main.cpp)
ra2_configure_sdl_app(shp_render_experiment)
target_compile_definitions(shp_render_experiment PRIVATE
  SHP_RENDER_EXPERIMENT_ROOT="${PROJECT_SOURCE_DIR}"
)

if(BUILD_TESTING)
  add_executable(ra2_gameplay_tests tests/gameplay_tests.cpp)
  add_executable(ra2_building_service_tests tests/building_service_tests.cpp)
  target_link_libraries(ra2_building_service_tests PRIVATE ra2_engine ra2_project_options)
  target_compile_definitions(ra2_building_service_tests PRIVATE SDL_MAIN_HANDLED)
  ra2_copy_runtime_dlls(ra2_building_service_tests)
  add_test(NAME building_service_regressions COMMAND ra2_building_service_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(building_service_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_defense_tests tests/defense_tests.cpp)
  target_link_libraries(ra2_defense_tests PRIVATE ra2_engine ra2_project_options)
  target_compile_definitions(ra2_defense_tests PRIVATE SDL_MAIN_HANDLED)
  ra2_copy_runtime_dlls(ra2_defense_tests)
  add_test(NAME defense_regressions COMMAND ra2_defense_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(defense_regressions PROPERTIES TIMEOUT 60)
  add_executable(ra2_unit_selection_tests tests/unit_selection_tests.cpp)
  target_link_libraries(ra2_unit_selection_tests PRIVATE ra2_engine ra2_project_options)
  target_compile_definitions(ra2_unit_selection_tests PRIVATE SDL_MAIN_HANDLED)
  ra2_copy_runtime_dlls(ra2_unit_selection_tests)
  add_test(NAME unit_selection_regressions COMMAND ra2_unit_selection_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  add_executable(ra2_tank_behavior_tests tests/tank_behavior_tests.cpp)
  target_link_libraries(ra2_tank_behavior_tests PRIVATE ra2_engine ra2_project_options)
  target_compile_definitions(ra2_tank_behavior_tests PRIVATE SDL_MAIN_HANDLED)
  ra2_copy_runtime_dlls(ra2_tank_behavior_tests)
  add_test(NAME tank_behavior_regressions COMMAND ra2_tank_behavior_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  target_compile_definitions(ra2_gameplay_tests PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ra2_gameplay_tests PRIVATE ra2_engine ra2_project_options)
  ra2_copy_runtime_dlls(ra2_gameplay_tests)
  add_test(NAME gameplay_regressions COMMAND ra2_gameplay_tests "${PROJECT_SOURCE_DIR}/assets/settings")
  set_tests_properties(gameplay_regressions PROPERTIES TIMEOUT 30)
  if(RA2_ENABLE_GRAPHICS_TESTS)
    add_test(NAME demo_sandbox COMMAND ra2_power_demo --sandbox-smoke-test)
    set_tests_properties(demo_sandbox PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_aircraft COMMAND ra2_power_demo --aircraft-smoke-test)
    set_tests_properties(demo_aircraft PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_naval_special COMMAND ra2_power_demo --naval-special-smoke-test)
    set_tests_properties(demo_naval_special PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_naval COMMAND ra2_power_demo --naval-smoke-test)
    set_tests_properties(demo_naval PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_commandos COMMAND ra2_power_demo --commando-smoke-test)
    set_tests_properties(demo_commandos PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_ivan_apocalypse COMMAND ra2_power_demo --ivan-apocalypse-smoke-test)
    set_tests_properties(demo_ivan_apocalypse PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_demolition COMMAND ra2_power_demo --demolition-smoke-test)
    set_tests_properties(demo_demolition PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_dog_drone COMMAND ra2_power_demo --dog-drone-smoke-test)
    set_tests_properties(demo_dog_drone PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_radiation_v3 COMMAND ra2_power_demo --radiation-v3-smoke-test)
    set_tests_properties(demo_radiation_v3 PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_tesla_units COMMAND ra2_power_demo --tesla-units-smoke-test)
    set_tests_properties(demo_tesla_units PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_building_actions COMMAND ra2_power_demo --building-actions-smoke-test)
    set_tests_properties(demo_building_actions PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_defense COMMAND ra2_power_demo --defense-smoke-test)
    set_tests_properties(demo_defense PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_mixed_sidebar COMMAND ra2_power_demo --sidebar-smoke-test)
    set_tests_properties(demo_mixed_sidebar PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_special_vehicles COMMAND ra2_power_demo --special-vehicles-smoke-test)
    set_tests_properties(demo_special_vehicles PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_air_defense COMMAND ra2_power_demo --air-defense-smoke-test)
    set_tests_properties(demo_air_defense PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_transport COMMAND ra2_power_demo --transport-smoke-test)
    set_tests_properties(demo_transport PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_support_infantry COMMAND ra2_power_demo --support-infantry-smoke-test)
    set_tests_properties(demo_support_infantry PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_infantry COMMAND ra2_power_demo --infantry-smoke-test)
    set_tests_properties(demo_infantry PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    add_test(NAME demo_startup COMMAND ra2_power_demo --smoke-test)
    add_test(NAME demo_simulation COMMAND ra2_power_demo --simulation-smoke-test)
    add_test(NAME demo_game_speed COMMAND ra2_power_demo --game-speed-smoke-test)
    add_test(NAME demo_building_presentation COMMAND ra2_power_demo --building-smoke-test)
    add_test(NAME demo_harvesting COMMAND ra2_power_demo --mining-smoke-test)
    add_test(NAME demo_tank_behavior COMMAND ra2_power_demo --tank-smoke-test)
    add_test(NAME demo_group_selection COMMAND ra2_power_demo --selection-smoke-test)
    # macOS may report an incomplete sampler binding without a failing exit code.
    set_tests_properties(demo_startup demo_simulation demo_game_speed
      demo_building_presentation demo_harvesting demo_tank_behavior demo_group_selection
      PROPERTIES FAIL_REGULAR_EXPRESSION "GLD_TEXTURE_INDEX_2D is unloadable")
    set_tests_properties(demo_group_selection PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_tank_behavior PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_harvesting PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_building_presentation PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_game_speed PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_simulation PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(demo_startup PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endif()
endif()
