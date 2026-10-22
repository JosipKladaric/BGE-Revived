# CMake toolchain for a MinGW-w64 GCC installation.
#
#   cmake -S . -B build/cmake-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-windows.cmake
#
# The compiler is discovered in the usual places, including the repository's own
# toolchain folder, so a portable MinGW can be dropped in without touching PATH.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(BGE_MINGW_HINTS
    "$ENV{BGE_MINGW_ROOT}"
    "$ENV{MINGW_ROOT}"
    "$ENV{MINGW64}"
    "${CMAKE_CURRENT_LIST_DIR}/../.toolchain/mingw64"
    "${CMAKE_CURRENT_LIST_DIR}/../mingw64"
    "${CMAKE_CURRENT_LIST_DIR}/../mingw-w64"
    "${CMAKE_CURRENT_LIST_DIR}/../mingw"
    "C:/mingw64"
    "C:/MinGW"
    "C:/msys64/mingw64")

find_program(BGE_GXX NAMES g++ g++.exe HINTS ${BGE_MINGW_HINTS} PATH_SUFFIXES bin)
find_program(BGE_GCC NAMES gcc gcc.exe HINTS ${BGE_MINGW_HINTS} PATH_SUFFIXES bin)
find_program(BGE_AR NAMES ar ar.exe HINTS ${BGE_MINGW_HINTS} PATH_SUFFIXES bin)
find_program(BGE_RANLIB NAMES ranlib ranlib.exe HINTS ${BGE_MINGW_HINTS} PATH_SUFFIXES bin)

if(NOT BGE_GXX)
  message(FATAL_ERROR "MinGW-w64 g++ not found. Set BGE_MINGW_ROOT to the toolchain root.")
endif()

set(CMAKE_C_COMPILER   "${BGE_GCC}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${BGE_GXX}" CACHE FILEPATH "")
if(BGE_AR)
  set(CMAKE_AR "${BGE_AR}" CACHE FILEPATH "")
endif()
if(BGE_RANLIB)
  set(CMAKE_RANLIB "${BGE_RANLIB}" CACHE FILEPATH "")
endif()

set(CMAKE_C_FLAGS   "-municode" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-municode" CACHE STRING "")
