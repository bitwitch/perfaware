@echo off
if not exist build\ mkdir build
pushd build
cl -Zi -W2 -nologo ..\decoder.c
popd
