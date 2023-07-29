#include <x86intrin.h>
#include <sys/time.h>

uint64_t os_timer_freq(void) {
	return 1000000;
}

uint64_t os_read_timer(void) {
	struct timeval value;
	gettimeofday(&value, 0);
	uint64_t result = os_timer_freq()*(uint64_t)value.tv_sec + (uint64_t)value.tv_usec;
	return result;
}

bool read_entire_file(char *filepath, char **file_data, size_t *out_size) {
	FILE *f = fopen(filepath, "rb");
	if (!f) {
		return false;
	}

	struct stat filestat;
	stat(filepath, &filestat);

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
	return true;
}
