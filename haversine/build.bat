@echo off
if not exist build\ mkdir build
pushd build
if "%1"=="generate" (
	cl /W3 /Zi /nologo "%~dp0generate_points.c"
) else (
	cl /W3 /Zi /nologo "%~dp0haversine.c"
)
popd
