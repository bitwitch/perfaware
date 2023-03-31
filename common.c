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

// dynamic array or "stretchy buffers", a la sean barrett
// ---------------------------------------------------------------------------

typedef struct {
	size_t len;
	size_t cap;
	char buf[]; // flexible array member
} DA_Header;

// get the metadata of the array which is stored before the actual buffer in memory
#define da__header(b) ((DA_Header*)((char*)b - offsetof(DA_Header, buf)))
// checks if n new elements will fit in the array
#define da__fits(b, n) (da_lenu(b) + (n) <= da_cap(b)) 
// if n new elements will not fit in the array, grow the array by reallocating 
#define da__fit(b, n) (da__fits(b, n) ? 0 : ((b) = da__grow((b), da_lenu(b) + (n), sizeof(*(b)))))

#define BUF(x) x // annotates that x is a stretchy buffer
#define da_len(b)  ((b) ? (int32_t)da__header(b)->len : 0)
#define da_lenu(b) ((b) ?          da__header(b)->len : 0)
#define da_set_len(b, l) da__header(b)->len = (l)
#define da_cap(b) ((b) ? da__header(b)->cap : 0)
#define da_end(b) ((b) + da_lenu(b))
#define da_push(b, ...) (da__fit(b, 1), (b)[da__header(b)->len++] = (__VA_ARGS__))
#define da_free(b) ((b) ? (free(da__header(b)), (b) = NULL) : 0)

void *da__grow(void *buf, size_t new_len, size_t elem_size) {
	size_t new_cap = MAX(1 + 2*da_cap(buf), new_len);
	assert(new_len <= new_cap);
	size_t new_size = offsetof(DA_Header, buf) + new_cap*elem_size;

	DA_Header *new_header;
	if (buf) {
		new_header = xrealloc(da__header(buf), new_size);
	} else {
		new_header = xmalloc(new_size);
		new_header->len = 0;
	}
	new_header->cap = new_cap;
	return new_header->buf;
}

// Arena Allocator
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
	da_push(arena->blocks, arena->ptr);
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
	for (int i=0; i<da_len(arena->blocks); ++i) {
		free(arena->blocks[i]);
	}
	da_free(arena->blocks);
}