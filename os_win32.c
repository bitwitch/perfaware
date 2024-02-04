#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#pragma comment (lib, "bcrypt.lib")

typedef struct {
	bool initialized;
	HANDLE process_handle;
} OS_Metrics;

OS_Metrics global_metrics;

void os_metrics_init(void) {
	if (!global_metrics.initialized) {
		global_metrics.initialized = true;
		global_metrics.process_handle = OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
			FALSE, 
			GetCurrentProcessId());
	}
}

U64 os_timer_freq(void) {
	LARGE_INTEGER Freq;
	QueryPerformanceFrequency(&Freq);
	return Freq.QuadPart;
}

U64 os_read_timer(void) {
	LARGE_INTEGER Value;
	QueryPerformanceCounter(&Value);
	return Value.QuadPart;
}

U64 os_file_size(char *filepath) {
	struct __stat64 stat = {0};
	_stat64(filepath, &stat);
	return stat.st_size;
}

U64 os_process_page_fault_count(void) {
	PROCESS_MEMORY_COUNTERS_EX counters = {0};
	if (!GetProcessMemoryInfo(global_metrics.process_handle, (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters))) {
		fprintf(stderr, "Error: failed to read process page fault count");
		return 0;
	}
	return counters.PageFaultCount;
}

U64 os_max_random_count(void) {
	// max size of ULONG
	return 0xffffffff;
}

bool os_random_bytes(void *dest, U64 dest_size) {
	U64 cursor = 0;
	U64 max_rand_count = os_max_random_count();

	while (cursor < dest_size) {
		BYTE *pos = (BYTE *)dest + cursor;
		ULONG size = (ULONG)min(dest_size - cursor, max_rand_count);
		if (BCryptGenRandom(0, pos, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
			return false;
		}
		cursor += size;
	}

	return true;
}

