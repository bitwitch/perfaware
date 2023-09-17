#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint64_t os_timer_freq(void) {
	LARGE_INTEGER Freq;
	QueryPerformanceFrequency(&Freq);
	return Freq.QuadPart;
}

uint64_t os_read_timer(void) {
	LARGE_INTEGER Value;
	QueryPerformanceCounter(&Value);
	return Value.QuadPart;
}

uint64_t os_file_size(char *filepath) {
	struct __stat64 stat = {0};
	_stat64(filepath, &stat);
	return stat.st_size;
}

