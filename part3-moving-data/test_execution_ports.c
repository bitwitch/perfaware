#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include "../common.c"

typedef uint8_t U8;
typedef uint32_t U32;
typedef uint64_t U64;
typedef double F64;

typedef enum { 
	TEST_MODE_UNINITIALIZED,
	TEST_MODE_TESTING,
	TEST_MODE_COMPLETED,
	TEST_MODE_ERROR,
} TestMode;

typedef struct {
	U64 total, min, max;
} RepetitionValue;

typedef struct {
	U64 test_count;
	RepetitionValue time;
	RepetitionValue page_faults;
} RepetitionTestResults;

typedef struct {
	TestMode mode;
	bool print_new_minimums;
	U64 target_processed_byte_count;
	U64 cpu_timer_freq;
	U64 try_for_time;
	U64 tests_started_at;
	U32 open_block_count;
	U32 close_block_count;
	U64 time_accumulated_this_test;
	U64 bytes_accumulated_this_test;
	U64 page_faults_accumulated_this_test;
	RepetitionTestResults results;
} RepetitionTester;

typedef struct {
	char *file_name;
	U8 *file_data;
	U64 file_size;
} ReadParams;

static F64 seconds_from_cpu_time(F64 cpu_time, U64 cpu_timer_freq) {
	F64 seconds = 0.0;
	if (cpu_timer_freq) {
		seconds = cpu_time / (F64)cpu_timer_freq;
	}
	return seconds;
}

static void print_single_result(char *label, F64 cpu_time, U64 cpu_timer_freq, U64 byte_count, F64 page_faults) {
	printf("%s: %.0f", label, cpu_time);
	if (cpu_timer_freq) {
		F64 seconds = seconds_from_cpu_time(cpu_time, cpu_timer_freq);
		printf(" (%fms)", 1000.0f * seconds);
	
		if (byte_count) {
			F64 gigabyte = (1024.0f * 1024.0f * 1024.0f);
			F64 best_bandwidth = byte_count / (gigabyte * seconds);
			printf(" %fgb/s", best_bandwidth);
		}
	}

	if (page_faults) {
		printf(" PF: %0.4f", page_faults);
		if (byte_count) {
			printf(" %0.4fk/fault", (F64)byte_count / (page_faults * 1024.0f));
		}
	}
}

static void print_results(RepetitionTestResults results, U64 cpu_timer_freq, U64 byte_count) {
	print_single_result("Min", (F64)results.time.min, cpu_timer_freq, byte_count, (F64)results.page_faults.min);
	printf("\n");
	
	print_single_result("Max", (F64)results.time.max, cpu_timer_freq, byte_count, (F64)results.page_faults.max);
	printf("\n");
	
	if(results.test_count) {
		F64 test_count = (F64)results.test_count;
		print_single_result("Avg", (F64)results.time.total / test_count, 
			cpu_timer_freq, byte_count, (F64)results.page_faults.total / test_count);
		printf("\n");
	}
}

static void error(RepetitionTester *tester, char *msg) {
	tester->mode = TEST_MODE_ERROR;
	fprintf(stderr, "Error: %s\n", msg);
}

static bool is_testing(RepetitionTester *tester) {
	if (tester->mode != TEST_MODE_TESTING)
		return false;

	U64 current_time = read_cpu_timer();

	if (tester->open_block_count) {
		if (tester->open_block_count != tester->close_block_count) {
			error(tester, "Unbalanced begin_time/end_time");
		}
		if (tester->bytes_accumulated_this_test != tester->target_processed_byte_count) {
			error(tester, "Processed byte count mismatch");
		}
		if (tester->mode == TEST_MODE_TESTING) {
			RepetitionTestResults *results = &tester->results;
			++results->test_count;
			U64 page_faults = tester->page_faults_accumulated_this_test;
			U64 elapsed = tester->time_accumulated_this_test;
			results->time.total += elapsed;
			results->page_faults.total += page_faults;
			if (elapsed > results->time.max) {
				results->time.max = elapsed;
				results->page_faults.max = page_faults;
			}
			if (elapsed < results->time.min) {
				results->time.min = elapsed;
				results->page_faults.min = page_faults;
				tester->tests_started_at = current_time;
				if (tester->print_new_minimums) {
					print_single_result("Min",
						(F64)results->time.min, 
						tester->cpu_timer_freq, 
						tester->bytes_accumulated_this_test,
						(F64)page_faults);
					printf("                                        \r");
				}
			}

			tester->open_block_count = 0;
			tester->close_block_count = 0;
			tester->time_accumulated_this_test = 0;
			tester->bytes_accumulated_this_test = 0;
			tester->page_faults_accumulated_this_test = 0;
		}
	}

	if ((current_time - tester->tests_started_at) > tester->try_for_time) {
		tester->mode = TEST_MODE_COMPLETED;
		printf("                                                          \r");
		print_results(tester->results, tester->cpu_timer_freq, tester->target_processed_byte_count);
	}

	return true;
}

static void new_test_wave(RepetitionTester *tester, U64 target_byte_count, U64 cpu_timer_freq, U32 seconds_to_try) {
	// reset state in tester 
	if (tester->mode == TEST_MODE_UNINITIALIZED) {
		tester->mode = TEST_MODE_TESTING;
		tester->target_processed_byte_count = target_byte_count;
		tester->cpu_timer_freq = cpu_timer_freq;
		tester->print_new_minimums = true;
		tester->results.time.min = (U64)-1;
	} else if (tester->mode == TEST_MODE_COMPLETED) {
		tester->mode = TEST_MODE_TESTING;
		if (tester->target_processed_byte_count != target_byte_count) {
			error(tester, "target_processed_byte_count changed");
		}
		if (tester->cpu_timer_freq != cpu_timer_freq) {
			error(tester, "cpu_timer_freq changed");
		}
	}

	tester->try_for_time = seconds_to_try * cpu_timer_freq;
	tester->tests_started_at = read_cpu_timer();
}

static void begin_time(RepetitionTester *tester) {
	++tester->open_block_count;
	tester->time_accumulated_this_test -= read_cpu_timer(); // implicitly compute difference between begin and end
	tester->page_faults_accumulated_this_test -= os_process_page_fault_count();

}

static void end_time(RepetitionTester *tester) {
	++tester->close_block_count;
	tester->time_accumulated_this_test += read_cpu_timer(); // implicitly compute difference between begin and end
	tester->page_faults_accumulated_this_test += os_process_page_fault_count();
}

static void count_bytes(RepetitionTester *tester, U64 byte_count) {
	tester->bytes_accumulated_this_test += byte_count;
}

typedef void ASMFunc(U64 iterations, U64 *value);

typedef struct {
	char *name;
	ASMFunc *func;
} TestFunc;

void load1(U64 iterations, U64 *value);
void load2(U64 iterations, U64 *value);
void load3(U64 iterations, U64 *value);
void load4(U64 iterations, U64 *value);
void store1(U64 iterations, U64 *ptr);
void store2(U64 iterations, U64 *ptr);
void store3(U64 iterations, U64 *ptr);
void store4(U64 iterations, U64 *ptr);
#pragma comment (lib, "movs_per_iteration")

TestFunc test_functions[] = {
	{"load1", load1},
	{"load2", load2},
	{"load3", load3},
	{"load4", load4},
	{"store1", store1},
	{"store2", store2},
	{"store3", store3},
	{"store4", store4},
};

int main(int argc, char **argv) {
	os_metrics_init();

	RepetitionTester testers[ARRAY_COUNT(test_functions)] = {0};
	U32 seconds_to_try = 10;
	U64 cpu_timer_freq = estimate_cpu_freq();

	printf("CPU freq: %fGHz\n", (double)cpu_timer_freq / (double)(1000*1000*1000));

	U64 value = 69;
	U64 iterations = 1000000;

	for (;;) {
		for (int i=0; i<ARRAY_COUNT(test_functions); ++i) {
			TestFunc test_func = test_functions[i];
			RepetitionTester *tester = &testers[i];

			printf("\n--- %s ---\n", test_func.name);
			new_test_wave(tester, iterations, cpu_timer_freq, seconds_to_try);
			while (is_testing(tester)) {
				begin_time(tester);
				test_func.func(iterations, &value);
				end_time(tester);
				count_bytes(tester, iterations);
			}
		}
	}

	return 0;
}


