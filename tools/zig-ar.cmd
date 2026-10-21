@echo off
rem CMake archiver wrapper: LLVM ar through the vendored Zig toolchain.
setlocal
set "BGE_ZIG=%~dp0..\.toolchain\zig\zig.exe"
if not exist "%BGE_ZIG%" set "BGE_ZIG=zig"
"%BGE_ZIG%" ar %*
