@echo off
if not exist build\ mkdir build
pushd build
cl /W3 /Zi /nologo "%~dp0generate_points.c"
popd
