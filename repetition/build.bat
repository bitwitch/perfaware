@echo off
setlocal
if not exist build\ mkdir build
pushd build

set "source_dir=%~dp0"

if "%1" == "fault_counter" (
	cl /W3 /WX /Zi /nologo "%source_dir%fault_counter.c"
) else if "%1" == "test_fread" (
	cl /W3 /WX /Zi /fsanitize=address /nologo "%source_dir%test_fread.c" 
	cl /O2 /W3 /WX /Zi /nologo /Fetest_fread_release "%source_dir%test_fread.c"
) else if "%1" == "test_asm_loops" (
	cl /W3 /WX /Zi /fsanitize=address /nologo "%source_dir%test_asm_loops.c" /link /LIBPATH:%source_dir%
	cl /O2 /W3 /WX /Zi /nologo /Fetest_asm_loops_release "%source_dir%test_asm_loops.c" /link /LIBPATH:%source_dir%
) else if "%1" == "test_front_end" (
	cl /W3 /WX /Zi /fsanitize=address /nologo "%source_dir%test_front_end.c" /link /LIBPATH:%source_dir%
	cl /O2 /W3 /WX /Zi /nologo /Fetest_front_end_release "%source_dir%test_front_end.c" /link /LIBPATH:%source_dir%
) else ( 
	cl /W3 /WX /Zi /fsanitize=address /nologo "%source_dir%test_branch_prediction.c" /link /LIBPATH:%source_dir%
	cl /O2 /W3 /WX /Zi /nologo /Fetest_branch_prediction_release "%source_dir%test_branch_prediction.c" /link /LIBPATH:%source_dir%
)

popd
