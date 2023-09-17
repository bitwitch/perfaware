@echo off
if not exist build\ mkdir build
pushd build
cl /W3 /WX /Zi /fsanitize=address /nologo "%~dp0test_fread.c"
popd
