#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <assert.h>

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
// File I/O
// ---------------------------------------------------------------------------

// this is the size of a chunk of data in each read. one is added to this in
// the actual call to fread to leave space for a null character
#ifndef READFILE_CHUNK
#define READFILE_CHUNK 2097152 // 2MiB
#endif

#define READ_ENTIRE_FILE_OK          0  /* Success */
#define READ_ENTIRE_FILE_INVALID    -1  /* Invalid parameters */
#define READ_ENTIRE_FILE_ERROR      -2  /* Stream error */
#define READ_ENTIRE_FILE_TOOMUCH    -3  /* Too much input */
#define READ_ENTIRE_FILE_NOMEM      -4  /* Out of memory */

int read_entire_file(FILE *fp, char **dataptr, size_t *sizeptr) {

    /*
     * See answer by Nominal Animal (note this is not the accepted answer)
     * https://stackoverflow.com/questions/14002954/c-programming-how-to-read-the-whole-file-contents-into-a-buffer#answer-44894946
     */

    char *data = NULL, *temp;
    uint64_t bytes_allocated = 0;
    uint64_t read_so_far = 0;
    uint64_t n; // bytes read in a single fread call

    /* None of the parameters can be NULL. */
    if (fp == NULL || dataptr == NULL || sizeptr == NULL)
        return READ_ENTIRE_FILE_INVALID;

    /* A read error already occurred? */
    if (ferror(fp))
        return READ_ENTIRE_FILE_ERROR;

    while (1) {
        /* first check if buffer is large enough to read another chunk */
        uint64_t new_size = read_so_far + READFILE_CHUNK + 1;

        if (bytes_allocated < new_size) {
            /* need to grow the buffer */
            bytes_allocated = new_size;

            /* overflow check */
            if (new_size <= read_so_far) {
                free(data);
                return READ_ENTIRE_FILE_TOOMUCH;
            }

            temp = realloc(data, new_size);
            if (!temp) {
                free(data);
                return READ_ENTIRE_FILE_NOMEM;
            }
            data = temp;
        }

        /* read in a chunk */
        n = fread(data+read_so_far, sizeof(char), READFILE_CHUNK, fp);
        if (n == 0)
            break;

        read_so_far += n;
    }

    if (ferror(fp)) {
        free(data);
        return READ_ENTIRE_FILE_ERROR;
    }

    /* resize the buffer to the exact length of the file (plus 1 for null termination) */
    temp = realloc(data, read_so_far + 1);
    if (!temp) {
        free(data);
        return READ_ENTIRE_FILE_NOMEM;
    }
    data = temp;
    data[read_so_far] = '\0';

    *dataptr = data;
    *sizeptr = read_so_far;
    return READ_ENTIRE_FILE_OK;
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
