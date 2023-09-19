@echo off
if not exist build\ mkdir build
pushd build
if "%1" == "fault_counter" (
	cl /W3 /WX /Zi /nologo "%~dp0fault_counter.c"
) else (
	cl /W3 /WX /Zi /fsanitize=address /nologo "%~dp0test_fread.c"
	cl /O2 /W3 /WX /Zi /nologo /Fetest_fread_release "%~dp0test_fread.c"
)
popd
