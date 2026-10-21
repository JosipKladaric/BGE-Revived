cmake_minimum_required(VERSION 3.20)

project(BGE_Revived VERSION 0.1.0 LANGUAGES C CXX)

option(BGE_WITH_PYTHON "Link the embedded CPython host" ON)
option(BGE_BUILD_SANDBOX "Build the sandbox application" ON)
option(BGE_BUILD_TESTS "Build the unit test runner" ON)
option(BGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- engine ----
file(GLOB_RECURSE BGE_ENGINE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp")
set(BGE_GLAD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/engine/third_party/glad/src/gl.c")

add_library(bge_engine STATIC ${BGE_ENGINE_SOURCES} ${BGE_GLAD_SOURCE})

target_include_directories(bge_engine PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/engine/third_party/glad/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/engine/third_party/stb"
  "${CMAKE_CURRENT_SOURCE_DIR}/engine/third_party")

if(MSVC)
  target_compile_options(bge_engine PRIVATE /W4 /permissive-)
  target_compile_definitions(bge_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(bge_engine PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    -Wno-unused-function -Wno-cast-function-type-mismatch)
endif()

if(BGE_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(bge_engine PRIVATE /WX)
  else()
    target_compile_options(bge_engine PRIVATE -Werror)
  endif()
endif()

if(WIN32)
  target_link_libraries(bge_engine PUBLIC opengl32 gdi32 user32 ole32 shell32 winmm ws2_32)
endif()

# --------------------------------------------------------------- python ----
if(BGE_WITH_PYTHON)
  find_package(Python3 COMPONENTS Development.Embed QUIET)
  if(Python3_FOUND)
    target_link_libraries(bge_engine PUBLIC Python3::Python)
    target_compile_definitions(bge_engine PUBLIC BGE_WITH_PYTHON=1)
    message(STATUS "BGE: embedding Python ${Python3_VERSION}")
  else()
    message(STATUS "BGE: Python development files not found, embedding disabled")
  endif()
endif()

# ------------------------------------------------------------- sandbox ----
if(BGE_BUILD_SANDBOX)
  file(GLOB_RECURSE BGE_SANDBOX_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/src/*.cpp")
  add_executable(bge_sandbox ${BGE_SANDBOX_SOURCES})
  target_link_libraries(bge_sandbox PRIVATE bge_engine)
  if(MSVC)
    target_compile_options(bge_sandbox PRIVATE /W4)
  else()
    target_compile_options(bge_sandbox PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endif()

# ---------------------------------------------------------- game build ----
# The runtime a packaged game ships with: it loads the scene next to the
# executable, so a game folder is self contained.
option(BGE_BUILD_GAME "Build the game runtime template" ON)
if(BGE_BUILD_GAME)
  file(GLOB_RECURSE BGE_GAME_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/game/src/*.cpp")
  add_executable(bge_game ${BGE_GAME_SOURCES})
  target_link_libraries(bge_game PRIVATE bge_engine)
  if(MSVC)
    target_compile_options(bge_game PRIVATE /W4)
  else()
    target_compile_options(bge_game PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endif()

# ---------------------------------------------------------------- tests ----
if(BGE_BUILD_TESTS)
  file(GLOB_RECURSE BGE_TEST_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/tests/src/*.cpp")
  add_executable(bge_tests ${BGE_TEST_SOURCES})
  target_link_libraries(bge_tests PRIVATE bge_engine)
  enable_testing()
  add_test(NAME bge_tests COMMAND bge_tests)
endif()

# --------------------------------------------------------------- assets ----
# The engine loads shaders from <asset_root>/shaders at runtime.
add_custom_target(bge_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
  COMMENT "Copying runtime assets")

if(BGE_BUILD_SANDBOX)
  add_dependencies(bge_sandbox bge_assets)
endif()
if(BGE_BUILD_GAME)
  add_dependencies(bge_game bge_assets)
endif()
if(BGE_BUILD_TESTS)
  add_dependencies(bge_tests bge_assets)
endif()
