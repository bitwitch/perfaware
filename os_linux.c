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

uint64_t os_file_size(char *filepath) {
	struct stat filestat;
	stat(filepath, &filestat);
	return stat.st_size;
}

