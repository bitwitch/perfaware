#include <intrin.h>
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

bool read_entire_file(char *filepath, char **file_data, size_t *out_size) {
	PROFILE_FUNCTION_BEGIN;
	printf("read_entire_file begin counter: %d\n", __COUNTER__);

	FILE *f = fopen(filepath, "rb");
	if (!f) {
		return false;
	}

	struct __stat64 stat;
	_stat64(filepath, &stat);

	size_t file_size = stat.st_size;

	*out_size = file_size + 1;
	*file_data = malloc(*out_size);
	if (!*file_data) {
		fclose(f);
		return false;
	}

	size_t bytes_read = fread(*file_data, 1, file_size, f);
	if (bytes_read < file_size && !feof(f)) {
		fclose(f);
		return false;
	}

	(*file_data)[bytes_read] = 0; // add null terminator
	fclose(f);

	printf("read_entire_file end counter: %d\n", __COUNTER__);
	PROFILE_FUNCTION_END;
	return true;
}
