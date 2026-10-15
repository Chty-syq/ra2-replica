cmake_minimum_required(VERSION 3.21)
project(ra2_01_setup LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Support both opening this directory and loading it from the repository root.
set(setup_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(setup_standalone ON)
endif()

# Only third-party dependencies are shared; no game libraries or assets are used.
set(SETUP_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SDL"
    CACHE PATH "SDL2 source directory")
set(SETUP_GLAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/glad"
    CACHE PATH "GLAD directory containing include/glad/glad.h and src/glad.cpp")
if(NOT EXISTS "${SETUP_GLAD_ROOT}/include/glad/glad.h" OR
   NOT EXISTS "${SETUP_GLAD_ROOT}/src/glad.cpp")
  message(FATAL_ERROR "GLAD sources missing. Set SETUP_GLAD_ROOT to the supplied GLAD directory.")
endif()

# Reuse SDL when embedded without overriding the parent project's build options.
if(NOT TARGET SDL2::SDL2)
  if(NOT EXISTS "${SETUP_SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 sources missing. Set SETUP_SDL_SOURCE_DIR to an SDL2 source directory.")
  endif()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_subdirectory("${SETUP_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/SDL" EXCLUDE_FROM_ALL)
endif()
if(TARGET SDL2::SDL2-static)
  set(setup_sdl_target SDL2::SDL2-static)
else()
  set(setup_sdl_target SDL2::SDL2)
endif()

find_package(OpenGL REQUIRED)
add_library(setup_glad STATIC "${SETUP_GLAD_ROOT}/src/glad.cpp")
target_include_directories(setup_glad SYSTEM PUBLIC "${SETUP_GLAD_ROOT}/include")
target_link_libraries(setup_glad PUBLIC ${setup_sdl_target})

set(setup_shaders
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.vert"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.frag"
)
set_source_files_properties(${setup_shaders} PROPERTIES HEADER_FILE_ONLY TRUE)
add_executable(ra2_01_setup main.cpp ${setup_shaders})
target_link_libraries(ra2_01_setup PRIVATE setup_glad OpenGL::GL)

# Also copy on builds that only change GLSL, without requiring a C++ relink.
add_custom_target(ra2_01_setup_shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ra2_01_setup>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${setup_shaders} "$<TARGET_FILE_DIR:ra2_01_setup>/shaders"
  VERBATIM
)
add_dependencies(ra2_01_setup ra2_01_setup_shaders)

if(MSVC)
  target_compile_options(ra2_01_setup PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(ra2_01_setup PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MINGW)
  # Make the executable runnable outside an IDE's compiler-specific PATH, too.
  target_link_options(ra2_01_setup PRIVATE -static)
endif()
if(WIN32 AND NOT TARGET SDL2::SDL2-static)
  add_custom_command(TARGET ra2_01_setup POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:ra2_01_setup>"
    VERBATIM
  )
endif()

if(setup_standalone)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ra2_01_setup)
  # Keep the generic run target local to the standalone project.
  add_custom_target(run
    COMMAND "$<TARGET_FILE:ra2_01_setup>"
    DEPENDS ra2_01_setup
    USES_TERMINAL
    VERBATIM
  )
endif()
