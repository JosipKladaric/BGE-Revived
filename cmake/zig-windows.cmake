# CMake toolchain that drives the vendored Zig toolchain.
#
#   cmake -S . -B build/cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake
#
# Zig bundles a recent Clang, so the engine builds with the same compiler and
# standard library as the build.py driver without needing Visual Studio.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

get_filename_component(BGE_ZIG_EXE "${CMAKE_CURRENT_LIST_DIR}/../.toolchain/zig/zig.exe" ABSOLUTE)
if(NOT EXISTS "${BGE_ZIG_EXE}")
  find_program(BGE_ZIG_EXE NAMES zig REQUIRED)
endif()

set(CMAKE_C_COMPILER   "${BGE_ZIG_EXE}" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${BGE_ZIG_EXE}" CACHE FILEPATH "C++ compiler")
set(CMAKE_C_COMPILER_ARG1   "cc"  CACHE STRING "C compiler mode")
set(CMAKE_CXX_COMPILER_ARG1 "c++" CACHE STRING "C++ compiler mode")

set(CMAKE_C_FLAGS   "-target x86_64-windows-gnu" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-target x86_64-windows-gnu" CACHE STRING "")

set(CMAKE_AR      "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ar.cmd" CACHE FILEPATH "")
set(CMAKE_RANLIB  "${CMAKE_CURRENT_LIST_DIR}/../tools/zig-ranlib.cmd" CACHE FILEPATH "")
# The compiler "ar" helpers are intentionally left empty: CMake calls them with
# "qc" for the ABI probe, which Zig does not implement.
set(CMAKE_C_COMPILER_AR   "" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER_AR "" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_RANLIB   "" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER_RANLIB "" CACHE FILEPATH "")
