@echo off
if not exist build\ mkdir build
pushd build
cl -Zi -W2 -nologo ..\cpu8086.c
popd
