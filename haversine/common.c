#define ARRAY_COUNT(a) sizeof(a)/sizeof(*(a))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

// Helper Utilities
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



// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

// TODO(shaw): null terminate the file data ??
bool read_entire_file(char *filepath, char **file_data, size_t *out_size) {
	FILE *f = fopen(filepath, "r");
	if (!f) {
		return false;
	}

	fseek(f, 0, SEEK_END);
	*out_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	*file_data = malloc(*out_size);
	if (!*file_data) {
		fclose(f);
		return false;
	}

	size_t bytes_read = fread(*file_data, 1, *out_size, f);
	printf("bytes_read=%zu out_size=%zu\n", bytes_read, *out_size);
	if (bytes_read < *out_size && !feof(f)) {
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
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

