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
	ALLOC_KIND_NONE,
	ALLOC_KIND_MALLOC,

	ALLOC_KIND_COUNT
} AllocKind;

typedef enum { 
	TEST_MODE_UNINITIALIZED,
	TEST_MODE_TESTING,
	TEST_MODE_COMPLETED,
	TEST_MODE_ERROR,
} TestMode;

typedef struct {
	U64 test_count;
	U64 total_time;
	U64 max_time;
	U64 min_time;
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
	RepetitionTestResults results;
} RepetitionTester;

typedef struct {
	AllocKind alloc_kind;
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

static void print_time(char *label, F64 cpu_time, U64 cpu_timer_freq, U64 byte_count) {
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
}

static void print_results(RepetitionTestResults results, U64 cpu_timer_freq, U64 byte_count) {
	print_time("Min", (F64)results.min_time, cpu_timer_freq, byte_count);
	printf("\n");
	
	print_time("Max", (F64)results.max_time, cpu_timer_freq, byte_count);
	printf("\n");
	
	if(results.test_count) {
		print_time("Avg", (F64)results.total_time / (F64)results.test_count, cpu_timer_freq, byte_count);
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
			U64 elapsed = tester->time_accumulated_this_test;
			++results->test_count;
			results->total_time += elapsed;
			if (elapsed > results->max_time) {
				results->max_time = elapsed;
			}
			if (elapsed < results->min_time) {
				results->min_time = elapsed;
				tester->tests_started_at = current_time;
				if (tester->print_new_minimums) {
					print_time("Min", (F64)results->min_time, tester->cpu_timer_freq, tester->bytes_accumulated_this_test);
					printf("               \r");
				}
			}

			tester->open_block_count = 0;
			tester->close_block_count = 0;
			tester->time_accumulated_this_test = 0;
			tester->bytes_accumulated_this_test = 0;
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
		tester->results.min_time = (U64)-1;
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
}

static void end_time(RepetitionTester *tester) {
	++tester->close_block_count;
	tester->time_accumulated_this_test += read_cpu_timer(); // implicitly compute difference between begin and end
}

static void count_bytes(RepetitionTester *tester, U64 byte_count) {
	tester->bytes_accumulated_this_test += byte_count;
}




static char *describe_alloc_kind(AllocKind kind) {
	char *result;
	switch (kind) {
		case ALLOC_KIND_NONE:   result = "";        break;
		case ALLOC_KIND_MALLOC: result = "malloc";  break;
		default:                result = "UNKNOWN"; break;
	}
	return result;
}

static U8 *handle_allocation(ReadParams *params) {
	U8 *result;
	switch (params->alloc_kind) {
	case ALLOC_KIND_NONE: 
		result = params->file_data;
		break;
	case ALLOC_KIND_MALLOC: 
		result = xmalloc(params->file_size);
		break;
	default:
		fprintf(stderr, "Unknown allocation kind in handle_allocation: %d\n", params->alloc_kind);
		result = NULL;
		break;
	}
	return result;
}

static void handle_deallocation(ReadParams *params, U8 *buffer) {
	switch (params->alloc_kind) {
	case ALLOC_KIND_NONE: 
		break;
	case ALLOC_KIND_MALLOC: 
		free(buffer);
		break;
	default:
		fprintf(stderr, "Unknown allocation kind in handle_deallocation: %d\n", params->alloc_kind);
		break;
	}
}

static void read_via_fread(RepetitionTester *tester, ReadParams *params) {
	while (is_testing(tester)) {
		FILE *file = fopen(params->file_name, "rb");
		if (file) {
			U8 *buffer = handle_allocation(params);

			begin_time(tester);
			size_t result = fread(buffer, params->file_size, 1, file);
			end_time(tester);

			if (result == 1) {
				count_bytes(tester, params->file_size);
			} else {
				error(tester, "fread failed");
			}

			handle_deallocation(params, buffer);

			fclose(file);
		} else {
			error(tester, "fopen failed");
		}
	}
}

static void read_via_read(RepetitionTester *tester, ReadParams *params) {
    while (is_testing(tester)) {
        int file = _open(params->file_name, _O_BINARY|_O_RDONLY);
        if (file != -1) {
			U8 *buffer = handle_allocation(params);
            U8 *dest = buffer;
            U64 size_remaining = params->file_size;

            while (size_remaining) {
				U32 read_size = INT_MAX;
				if ((U64)read_size > size_remaining) {
                    read_size = (U32)size_remaining;
                }

                begin_time(tester);
                int result = _read(file, dest, read_size);
                end_time(tester);

                if (result == (int)read_size) {
                    count_bytes(tester, read_size);
                } else {
                    error(tester, "_read failed");
                    break;
                }
                
                size_remaining -= read_size;
                dest += read_size;
            }
            
			handle_deallocation(params, buffer);
            _close(file);
        } else {
            error(tester, "_open failed");
        }
    }
}

static void read_via_ReadFile(RepetitionTester *tester, ReadParams *params) {
    while (is_testing(tester)) {
        HANDLE file = CreateFileA(params->file_name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) {
			U8 *buffer = handle_allocation(params);
            U8 *dest = buffer;
            U64 size_remaining = params->file_size;

            while (size_remaining) {
                U32 read_size = (U32)-1;
                if ((U64)read_size > size_remaining) {
                    read_size = (U32)size_remaining;
                }
                
                DWORD bytes_read = 0;
                begin_time(tester);
                bool result = ReadFile(file, dest, read_size, &bytes_read, 0);
                end_time(tester);
                
                if (result && (bytes_read == read_size)) {
                    count_bytes(tester, read_size);
                } else {
                    error(tester, "ReadFile failed");
                }
                
                size_remaining -= read_size;
                dest += read_size;
            }
            
			handle_deallocation(params, buffer);
            CloseHandle(file);
        } else {
            error(tester, "CreateFileA failed");
        }
    }
}



typedef void ReadOverheadTestFunc(RepetitionTester *tester, ReadParams *params);

typedef struct {
	char *name;
	ReadOverheadTestFunc *func;
} TestFunc;

TestFunc test_functions[] = {
	{"fread", read_via_fread},
	{"_read", read_via_read},
	{"ReadFile", read_via_ReadFile},
};

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s [existing filename]\n", argv[0]);
		exit(1);
	}
	char *file_name = argv[1];

	RepetitionTester testers[ARRAY_COUNT(test_functions)][ALLOC_KIND_COUNT] = {0};
	U32 seconds_to_try = 10;
	U64 cpu_timer_freq = estimate_cpu_freq();

	U64 file_size = os_file_size(file_name);
	if (file_size == 0) {
		fprintf(stderr, "Error: test data size must be non-zero\n");
		exit(1);
	}

	ReadParams params = { 
		.file_name = file_name,
		.file_data = xmalloc(file_size),
		.file_size = file_size,
	};

	for (;;) {
		for (int i=0; i<ARRAY_COUNT(test_functions); ++i) {
				TestFunc test_func = test_functions[i];
			for (int j=0; j<ALLOC_KIND_COUNT; ++j) {
				RepetitionTester *tester = &testers[i][j];
				params.alloc_kind = (AllocKind)j;
				printf("\n--- %s%s%s ---\n", 
						describe_alloc_kind(params.alloc_kind),
						params.alloc_kind ? " + " : "",
						test_func.name);
				new_test_wave(tester, params.file_size, cpu_timer_freq, seconds_to_try);
				test_func.func(tester, &params);
			}
		}
	}

	return 0;
}


