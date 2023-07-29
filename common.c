#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/stat.h>
#include "os.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ARRAY_COUNT(a) sizeof(a)/sizeof(*(a))

// ---------------------------------------------------------------------------
// Helper Utilities
// ---------------------------------------------------------------------------
void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("malloc");
        exit(1);
    }
    return ptr;
}

void *xcalloc(size_t num_items, size_t item_size) {
    void *ptr = calloc(num_items, item_size);
    if (ptr == NULL) {
        perror("calloc");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (result == NULL) {
        perror("recalloc");
        exit(1);
    }
    return result;
}

void fatal(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("FATAL: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    exit(1);
}

char *chop_by_delimiter(char **str, char *delimiter) {
    char *chopped = *str;

    char *found = strstr(*str, delimiter);
    if (found == NULL) {
        *str += strlen(*str);
        return chopped;
    }

    *found = '\0';
    *str = found + strlen(delimiter);

    return chopped;
}


// ---------------------------------------------------------------------------
// stretchy buffer, a la sean barrett
// ---------------------------------------------------------------------------
typedef struct {
	size_t len;
	size_t cap;
	char buf[]; // flexible array member
} BufHeader;

// get the metadata of the array which is stored before the actual buffer in memory
#define buf__header(b) ((BufHeader*)((char*)b - offsetof(BufHeader, buf)))
// checks if n new elements will fit in the array
#define buf__fits(b, n) (buf_lenu(b) + (n) <= buf_cap(b)) 
// if n new elements will not fit in the array, grow the array by reallocating 
#define buf__fit(b, n) (buf__fits(b, n) ? 0 : ((b) = buf__grow((b), buf_lenu(b) + (n), sizeof(*(b)))))

#define BUF(x) x // annotates that x is a stretchy buffer
#define buf_len(b)  ((b) ? (int)buf__header(b)->len : 0)
#define buf_lenu(b) ((b) ?      buf__header(b)->len : 0)
#define buf_set_len(b, l) buf__header(b)->len = (l)
#define buf_cap(b) ((b) ? buf__header(b)->cap : 0)
#define buf_end(b) ((b) + buf_lenu(b))
#define buf_push(b, ...) (buf__fit(b, 1), (b)[buf__header(b)->len++] = (__VA_ARGS__))
#define buf_free(b) ((b) ? (free(buf__header(b)), (b) = NULL) : 0)
#define buf_printf(b, ...) ((b) = buf__printf((b), __VA_ARGS__))

void *buf__grow(void *buf, size_t new_len, size_t elem_size) {
	size_t new_cap = MAX(1 + 2*buf_cap(buf), new_len);
	assert(new_len <= new_cap);
	size_t new_size = offsetof(BufHeader, buf) + new_cap*elem_size;

	BufHeader *new_header;
	if (buf) {
		new_header = xrealloc(buf__header(buf), new_size);
	} else {
		new_header = xmalloc(new_size);
		new_header->len = 0;
	}
	new_header->cap = new_cap;
	return new_header->buf;
}

char *buf__printf(char *buf, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int add_size = 1 + vsnprintf(NULL, 0, fmt, args);
    va_end(args);

	int cur_len = buf_len(buf);

	buf__fit(buf, add_size);

	char *start = cur_len ? buf + cur_len - 1 : buf;
    va_start(args, fmt);
    vsnprintf(start, add_size, fmt, args);
    va_end(args);

	// if appending to a string that is already null terminated, we clobber the
	// original null terminator so we need to subtract 1
	buf__header(buf)->len += cur_len ? add_size - 1 : add_size;

	return buf;
}

// ---------------------------------------------------------------------------
// Arena Allocator
// ---------------------------------------------------------------------------
#define ARENA_BLOCK_SIZE 65536

typedef struct {
	char *ptr;
	char *end;
	BUF(char **blocks);
} Arena;

void arena_grow(Arena *arena, size_t min_size) {
	size_t size = MAX(ARENA_BLOCK_SIZE, min_size);
	arena->ptr = xmalloc(size);
	arena->end = arena->ptr + size;
	buf_push(arena->blocks, arena->ptr);
}

void *arena_alloc(Arena *arena, size_t size) {
	if (arena->ptr + size > arena->end) {
		arena_grow(arena, size); 
	}
	void *ptr = arena->ptr;
	arena->ptr += size;
	return ptr;
}

void *arena_alloc_zeroed(Arena *arena, size_t size) {
	void *ptr = arena_alloc(arena, size);
	memset(ptr, 0, size);
	return ptr;
}

void arena_free(Arena *arena) {
	for (int i=0; i<buf_len(arena->blocks); ++i) {
		free(arena->blocks[i]);
	}
	buf_free(arena->blocks);
}

// ---------------------------------------------------------------------------
// Timers and Profiling
// ---------------------------------------------------------------------------
uint64_t read_cpu_timer(void) {
	return __rdtsc();
}

uint64_t estimate_cpu_freq(void) {
	uint64_t wait_time_ms = 100;
	uint64_t os_freq = os_timer_freq();
	uint64_t os_ticks_during_wait_time = os_freq * wait_time_ms / 1000;
	uint64_t os_elapsed = 0;
	uint64_t os_start = os_read_timer();
	uint64_t cpu_start = read_cpu_timer();

	while (os_elapsed < os_ticks_during_wait_time) {
		os_elapsed = os_read_timer() - os_start;
	}

	uint64_t cpu_freq = (read_cpu_timer() - cpu_start) * os_freq / os_elapsed;

	return cpu_freq;
}

typedef struct {
	char *name;
	int count;
	uint64_t total_elapsed;
} ProfileBlockInfo;

typedef struct {
	char *name;
	int counter;
	uint64_t start, stop;
} ProfileTsPair;

BUF(ProfileBlockInfo *profile_info);
BUF(ProfileTsPair *profile_timestamps);
uint64_t profile_start;


// FIXME(shaw): all of these linear searches in the profiling utility are going to scale really poorly
// and be slow af. they are just used to prove out a concept quickly.

ProfileTsPair *get_ts_pair(char *name, int counter) {
	for (int i=0; i<buf_len(profile_timestamps); ++i) {
		if (profile_timestamps[i].counter == counter && strcmp(profile_timestamps[i].name, name) == 0) {
			return &profile_timestamps[i];
		}
	}
	return NULL;
}

// increments the count of the function identified by name and returns the
// previous function call count
int increment_func_call_count(char *name) {
	for (int i=0; i<buf_len(profile_info); ++i) {
		if (strcmp(profile_info[i].name, name) == 0) {
			int prev_count = profile_info[i].count;
			++profile_info[i].count;
			return prev_count;
		}
	}

	// not found, create new entry
	buf_push(profile_info, (ProfileBlockInfo){.name=name, .count=1});
	return 0;
}

void record_profile_block_time(char *name, uint64_t elapsed) {
	for (int i=0; i<buf_len(profile_info); ++i) {
		if (strcmp(profile_info[i].name, name) == 0) {
			profile_info[i].total_elapsed += elapsed;
			return;
		}
	}
	// couldn't find function with name in profile_info, should never happen
	assert(0);
}

// NOTE(shaw): this macro is not guarded with typical do-while because it relies on __profile_count
// being defined in the associated PROFILE_FUNCTION_END
#define PROFILE_FUNCTION_BEGIN \
	int __profile_count = increment_func_call_count(__func__); \
	buf_push(profile_timestamps, (ProfileTsPair){.name = __func__, .counter = __profile_count, .start = read_cpu_timer()});

#define PROFILE_FUNCTION_END do { \
	ProfileTsPair *ts_pair = get_ts_pair(__func__, __profile_count); \
	if (ts_pair) { \
		ts_pair->stop = read_cpu_timer(); \
		record_profile_block_time(__func__, ts_pair->stop - ts_pair->start); \
	} \
} while(0)

#define PROFILE_BLOCK_BEGIN(block_name) \
	buf_push(profile_timestamps, (ProfileTsPair){.name = block_name, .start = read_cpu_timer()})

#define PROFILE_BLOCK_END(block_name) do { \
	ProfileTsPair *ts_pair = get_ts_pair(block_name, 0); \
	if (ts_pair) { \
		ts_pair->stop = read_cpu_timer(); \
		record_profile_block_time(block_name, ts_pair->stop - ts_pair->start); \
	} \
} while(0)


void begin_profile(void) {
	profile_start = read_cpu_timer();
}

void end_profile(void) {
	uint64_t total_ticks = read_cpu_timer() - profile_start;
	assert(total_ticks);
	uint64_t cpu_freq = estimate_cpu_freq();
	assert(cpu_freq);
	double total_ms = 1000 * (total_ticks / (double)cpu_freq);

	printf("\nTotal time: %fms (cpu freq %llu)\n", total_ms, cpu_freq);

	for (int i=0; i<buf_len(profile_info); ++i) {
		ProfileBlockInfo info = profile_info[i];
		double pct = 100 * (info.total_elapsed / (double)total_ticks);
		printf("\t%s: %llu (%.2f%%)\n", info.name, info.total_elapsed, pct);
	}
}
	
// ---------------------------------------------------------------------------
// OS Specific Functions
// ---------------------------------------------------------------------------

#undef MIN
#undef MAX
#undef ARRAY_COUNT

#if _WIN32
	#include "os_win32.c"
#else
	#include "os_linux.c"
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ARRAY_COUNT(a) sizeof(a)/sizeof(*(a))




