@echo off
if not exist build\ mkdir build
pushd build
cl -Zi -W2 -nologo "%~dp0cpu8086.c"
popd
