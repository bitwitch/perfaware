#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

/*

json: dict
      array

expr: dict
	  array
	  string
	  float

dict: '{' string ':' expr [',' string ':' expr]* '}'
array: '[' expr? [',' expr]* ']'

*/

typedef enum {
	TOKEN_NONE,
	TOKEN_LEFT_BRACE,
	TOKEN_RIGHT_BRACE,
	TOKEN_STRING,
	TOKEN_COLON,
	TOKEN_LEFT_BRACKET,
	TOKEN_RIGHT_BRACKET,
	TOKEN_FLOAT,
	TOKEN_COMMA,
} TokenKind;

typedef struct {
	TokenKind kind;
	char *str_val;
	float float_val;
	// char char_val;
} Token;


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

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s [haversine_input.json]\n", argv[0]);
		printf("       %s [haversine_input.json] [answers.f64]\n", argv[0]);
		exit(1);
	}

	char *input_filepath = argv[1];
	char *answers_filepath = argc > 2 ? argv[2] : NULL;

	printf("input filepath: %s\n", input_filepath);
	printf("answers filepath: %s\n", answers_filepath);

	char *file_data;
	size_t file_size;
	bool ok = read_entire_file(input_filepath, &file_data, &file_size);
	if (!ok) {
		perror("read_entire_file");
		exit(1);
	}

	printf("Successfully read file\n");

	return 0;
}

