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
	uint64_t ticks_exclusive; // without children
	uint64_t ticks_inclusive; // with children
} ProfileBlock;

typedef struct {
	char *name;
	uint64_t start, stop;
} ProfileTsPair;

ProfileBlock profile_blocks[4096];
uint64_t profile_start; 
uint64_t current_profile_block_index;

// NOTE(shaw): This macro is not guarded with typical do-while because it relies on 
// variables declared inside it being accessible in the associated PROFILE_BLOCK_END.
// This means that you cannot have nested profile blocks in the same scope.
// However, in most cases you either already have separate scopes, or you
// should trivially be able to open a new scope {}.
#define PROFILE_BLOCK_BEGIN(block_name) \
	char *__block_name = block_name; \
	uint64_t __block_index = __COUNTER__ + 1; \
	uint64_t __parent_index = current_profile_block_index; \
	current_profile_block_index = __block_index; \
	ProfileBlock *__block = &profile_blocks[__block_index]; \
	uint64_t __top_level_sum = __block->ticks_inclusive; \
	uint64_t __block_start = read_cpu_timer();

#define PROFILE_BLOCK_END do { \
	uint64_t elapsed = read_cpu_timer() - __block_start; \
	__block->name = __block_name; \
	++__block->count; \
	__block->ticks_inclusive = __top_level_sum + elapsed; \
	__block->ticks_exclusive += elapsed; \
	profile_blocks[__parent_index].ticks_exclusive -= elapsed; \
	current_profile_block_index = __parent_index; \
} while(0)

#define PROFILE_FUNCTION_BEGIN PROFILE_BLOCK_BEGIN(__func__)
#define PROFILE_FUNCTION_END PROFILE_BLOCK_END

void begin_profile(void) {
	profile_start = read_cpu_timer();
}

void end_profile(void) {
	uint64_t total_ticks = read_cpu_timer() - profile_start;
	assert(total_ticks);
	uint64_t cpu_freq = estimate_cpu_freq();
	assert(cpu_freq);
	double total_ms = 1000 * (total_ticks / (double)cpu_freq);

	printf("\nTotal time: %f ms %llu ticks (cpu freq %llu)\n", total_ms, total_ticks, cpu_freq);

	for (int i=0; i<ARRAY_COUNT(profile_blocks); ++i) {
		ProfileBlock block = profile_blocks[i];
		if (!block.ticks_inclusive) continue;

		double pct_exclusive = 100 * (block.ticks_exclusive / (double)total_ticks);
		printf("\t%s[%d]: exclusive %.2f%%, %llu ticks",
				block.name, block.count, pct_exclusive, block.ticks_exclusive);

		if (block.ticks_exclusive != block.ticks_inclusive) {
			double pct_inclusive = 100 * (block.ticks_inclusive / (double)total_ticks);
			printf(" | inclusive %.2f%%, %llu ticks", pct_inclusive, block.ticks_inclusive);
		} 

		printf("\n");
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




