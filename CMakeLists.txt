cmake_minimum_required(VERSION 3.21)

project(ra2_replica_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(OpenGL REQUIRED)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
  message(FATAL_ERROR "SDL2 source is missing. Please place it under third_party/SDL.")
endif()

add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

add_executable(ra2_power_demo
  src/app_main_ui.cpp
  src/art_ini.cpp
  src/building_system_runtime.cpp
  src/demo_app_support.cpp
  src/demo_scene.cpp
  src/gl_loader.cpp
  src/imgui_debug_panel.cpp
  src/iso_grid_renderer.cpp
  src/iso_world.cpp
  src/map_grid.cpp
  src/palette.cpp
  src/renderer2d.cpp
  src/rhino_unit.cpp
  src/rhino_render_state.cpp
  src/rules_colors.cpp
  src/sidebar.cpp
  src/shp_ts.cpp
  src/tmp_tile.cpp
  src/vpl_box_renderer.cpp
  src/voxel_unit.cpp
  src/vpl_file.cpp
  src/vxl_file.cpp
  third_party/glad/src/glad.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ra2_power_demo PRIVATE
  src
  third_party/glad/include
  third_party/imgui
  third_party/imgui/backends
)

add_executable(vxl_render_experiment
  experiment/vxl-render/src/main.cpp
  src/gl_loader.cpp
  src/palette.cpp
  src/vpl_box_renderer.cpp
  src/vpl_file.cpp
  src/vxl_file.cpp
  third_party/glad/src/glad.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(vxl_render_experiment PRIVATE
  src
  experiment/vxl-render/src
  third_party/glad/include
  third_party/imgui
  third_party/imgui/backends
)

target_compile_definitions(vxl_render_experiment PRIVATE
  VXL_RENDER_EXPERIMENT_ROOT="${CMAKE_SOURCE_DIR}/experiment/vxl-render"
)

if(MINGW)
  target_link_libraries(ra2_power_demo PRIVATE
    OpenGL::GL
    mingw32
    SDL2::SDL2main
    SDL2::SDL2
  )
  target_link_libraries(vxl_render_experiment PRIVATE
    OpenGL::GL
    mingw32
    SDL2::SDL2main
    SDL2::SDL2
  )
else()
  target_link_libraries(ra2_power_demo PRIVATE
    OpenGL::GL
    SDL2::SDL2
    SDL2::SDL2main
  )
  target_link_libraries(vxl_render_experiment PRIVATE
    OpenGL::GL
    SDL2::SDL2
    SDL2::SDL2main
  )
endif()

if(WIN32)
  add_custom_command(TARGET ra2_power_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:ra2_power_demo>
      $<TARGET_FILE_DIR:ra2_power_demo>
    COMMAND_EXPAND_LISTS
  )
  add_custom_command(TARGET vxl_render_experiment POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:vxl_render_experiment>
      $<TARGET_FILE_DIR:vxl_render_experiment>
    COMMAND_EXPAND_LISTS
  )
endif()
