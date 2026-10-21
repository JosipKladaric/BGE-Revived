@echo off
rem CMake ranlib wrapper: "zig ar s" writes the archive symbol index.
setlocal
set "BGE_ZIG=%~dp0..\.toolchain\zig\zig.exe"
if not exist "%BGE_ZIG%" set "BGE_ZIG=zig"
"%BGE_ZIG%" ar s %*
