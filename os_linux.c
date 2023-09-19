#include <x86intrin.h>
#include <sys/time.h>

void os_metrics_init(void) {
	assert(0 && "Not implemented");
}

U64 os_process_page_fault_count(void) {
	assert(0 && "Not implemented");
	return 0;
}

U64 os_timer_freq(void) {
	return 1000000;
}

U64 os_read_timer(void) {
	struct timeval value;
	gettimeofday(&value, 0);
	uint64_t result = os_timer_freq()*(U64)value.tv_sec + (U64)value.tv_usec;
	return result;
}

U64 os_file_size(char *filepath) {
	struct stat filestat;
	stat(filepath, &filestat);
	return stat.st_size;
}

