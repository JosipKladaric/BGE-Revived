@echo off
rem CMake compiler wrapper: drives the vendored Zig toolchain in C++ mode.
setlocal
set "BGE_ZIG=%~dp0..\.toolchain\zig\zig.exe"
if not exist "%BGE_ZIG%" set "BGE_ZIG=zig"
"%BGE_ZIG%" c++ %*
