cmake_minimum_required(VERSION 3.21)
project(ra2_02_static_resource_rendering LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Support both opening this directory and loading it from the repository root.
set(static_standalone OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(static_standalone ON)
endif ()

# Only third-party dependencies are shared; no game libraries are used.
set(STATIC_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SDL"
        CACHE PATH "SDL2 source directory")
set(STATIC_GLAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/glad"
        CACHE PATH "GLAD directory containing include/glad/glad.h and src/glad.cpp")
set(STATIC_GLM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/glm"
        CACHE PATH "GLM directory containing glm/glm.hpp")
if (NOT EXISTS "${STATIC_GLAD_ROOT}/include/glad/glad.h" OR
        NOT EXISTS "${STATIC_GLAD_ROOT}/src/glad.cpp")
    message(FATAL_ERROR "GLAD sources missing. Set STATIC_GLAD_ROOT to the supplied GLAD directory.")
endif ()

if (NOT EXISTS "${STATIC_GLM_ROOT}/glm/glm.hpp")
    message(FATAL_ERROR "GLM headers missing. Set STATIC_GLM_ROOT to the GLM directory.")
endif ()
add_library(static_glm INTERFACE)
target_include_directories(static_glm SYSTEM INTERFACE "${STATIC_GLM_ROOT}")

# Reuse SDL when embedded without overriding the parent project's build options.
if (NOT TARGET SDL2::SDL2)
    if (NOT EXISTS "${STATIC_SDL_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "SDL2 sources missing. Set STATIC_SDL_SOURCE_DIR to an SDL2 source directory.")
    endif ()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    if (MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif ()
    add_subdirectory("${STATIC_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/SDL" EXCLUDE_FROM_ALL)
endif ()
if (TARGET SDL2::SDL2-static)
    set(static_sdl_target SDL2::SDL2-static)
else ()
    set(static_sdl_target SDL2::SDL2)
endif ()

find_package(OpenGL REQUIRED)
add_library(static_glad STATIC "${STATIC_GLAD_ROOT}/src/glad.cpp")
target_include_directories(static_glad SYSTEM PUBLIC "${STATIC_GLAD_ROOT}/include")
target_link_libraries(static_glad PUBLIC ${static_sdl_target})

set(static_shaders
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sprite.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sprite.frag"
)
set_source_files_properties(${static_shaders} PROPERTIES HEADER_FILE_ONLY TRUE)
add_executable(ra2_02_static_resource_rendering
        main.cpp shader.hpp texture.hpp shp.hpp palette.hpp ${static_shaders})
target_link_libraries(ra2_02_static_resource_rendering PRIVATE static_glad static_glm OpenGL::GL)

# Also copy on builds that only change GLSL, without requiring a C++ relink.
add_custom_target(ra2_02_static_resource_rendering_resources
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ra2_02_static_resource_rendering>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${static_shaders} "$<TARGET_FILE_DIR:ra2_02_static_resource_rendering>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ra2_02_static_resource_rendering>/assets"
        VERBATIM
)
add_dependencies(ra2_02_static_resource_rendering ra2_02_static_resource_rendering_resources)

if (MSVC)
    target_compile_options(ra2_02_static_resource_rendering PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(ra2_02_static_resource_rendering PRIVATE -Wall -Wextra -Wpedantic)
endif ()
if (MINGW)
    # Make the executable runnable outside an IDE's compiler-specific PATH, too.
    target_link_options(ra2_02_static_resource_rendering PRIVATE -static)
endif ()
if (WIN32 AND NOT TARGET SDL2::SDL2-static)
    add_custom_command(TARGET ra2_02_static_resource_rendering POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:ra2_02_static_resource_rendering>"
            VERBATIM
    )
endif ()

if (static_standalone)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ra2_02_static_resource_rendering)
    # Keep the generic run target local to the standalone project.
    add_custom_target(run
            COMMAND "$<TARGET_FILE:ra2_02_static_resource_rendering>"
            DEPENDS ra2_02_static_resource_rendering
            USES_TERMINAL
            VERBATIM
    )
endif ()
