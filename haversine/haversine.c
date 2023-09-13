#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define PROFILE 1
#include "../common.c"

#define EARTH_RADIUS_KM 6372.8

typedef double F64;

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
	F64 float_val;
} Token;

typedef enum {
	EXPR_NONE,
	EXPR_DICT,
	EXPR_ARRAY,
	EXPR_STRING,
	EXPR_FLOAT,
} ExprKind;

typedef struct JsonExpr JsonExpr;

typedef struct {
	char *key;
	JsonExpr *val;
} Entry;

typedef struct {
	Entry *entries;	
	size_t num_entries;
} JsonDict;

typedef struct {
	JsonExpr **items;	
	size_t num_items;
} JsonArray;

struct JsonExpr {
	ExprKind kind;
	union {
		JsonDict dict;
		JsonArray array;
		char *str_val;
		F64 float_val;
	};
};

typedef struct {
	F64 x0, y0, x1, y1;
} Pair;

typedef struct {
	Pair *pairs;
	size_t num_pairs;
} HaversineInput;

JsonExpr *dict_get(JsonDict dict, char *key) {
	// NOTE(shaw): slow linear search since dict is an array of key value pairs right now
	for (int i=0; i < dict.num_entries; ++i) {
		Entry entry = dict.entries[i];
		if (strcmp(entry.key, key) == 0) {
			return entry.val;
		}
	}
	return NULL;
}

//------------------------------------------------------------------------------
// Node Allocation
//------------------------------------------------------------------------------
JsonExpr *expr_dict(Entry *entries, size_t num_entries) {
	JsonExpr *expr = xmalloc(sizeof(JsonExpr));
	expr->kind = EXPR_DICT;
	expr->dict.entries = entries;
	expr->dict.num_entries = num_entries;
	return expr;
}


JsonExpr *expr_array(JsonExpr **items, size_t num_items) {
	JsonExpr *expr = xmalloc(sizeof(JsonExpr));
	expr->kind = EXPR_ARRAY;
	expr->array.items = items;
	expr->array.num_items = num_items;
	return expr;
}

JsonExpr *expr_string(char *str) {
	JsonExpr *expr = xmalloc(sizeof(JsonExpr));
	expr->kind = EXPR_STRING;
	expr->str_val = str;
	return expr;
}

JsonExpr *expr_float(F64 val) {
	JsonExpr *expr = xmalloc(sizeof(JsonExpr));
	expr->kind = EXPR_FLOAT;
	expr->float_val = val;
	return expr;
}

//------------------------------------------------------------------------------
// Parsing
//------------------------------------------------------------------------------
char *stream;
Token token;

void next_token(void);

void init_parse(char *data) {
	stream = data;
	next_token();
}

void parse_error(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "json parse error: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(1);
}

bool is_token(TokenKind kind) {
	return token.kind == kind;
}

bool match_token(TokenKind kind) {
	if (token.kind == kind) {
		next_token();
		return true;
	}
	return false;
}

void expect_token(TokenKind kind) {
	if (token.kind == kind) {
		next_token();
	} else {
		// TODO(shaw): better error message which gets a string from the token kind
		parse_error("expected token kind %d, got %d", kind, token.kind);
	}
}

void scan_str(void) {
	token.kind = TOKEN_STRING;
	++stream;
	char *start = stream;
	while (*stream != '"') {
		++stream;
	}
	*stream = 0; // add null terminator
	++stream;
	token.str_val = start;
}

void scan_float(void) {
    char *start = stream;
	if (*stream == '-')
        ++stream;
    while (isdigit(*stream))
        ++stream;
    if (*stream == '.')
        ++stream;
    while (isdigit(*stream))
        ++stream;
    F64 val = strtod(start, NULL);
    if (val == HUGE_VAL || val == -HUGE_VAL)
        parse_error("Float literal overflow");
    token.kind = TOKEN_FLOAT;
    token.float_val = val;
}

void next_token(void) {
	// eat spaces
	while (isspace(*stream)) {
		++stream;
	}

	switch (*stream) {
		case '{': 
			token.kind = TOKEN_LEFT_BRACE;
			++stream;
			break;
		case '}': 
			token.kind = TOKEN_RIGHT_BRACE;
			++stream;
			break;
		case '[': 
			token.kind = TOKEN_LEFT_BRACKET;
			++stream;
			break;
		case ']': 
			token.kind = TOKEN_RIGHT_BRACKET;
			++stream;
			break;
		case ',': 
			token.kind = TOKEN_COMMA;
			++stream;
			break;
		case ':': 
			token.kind = TOKEN_COLON;
			++stream;
			break;
		case '"':
			scan_str();
			break;
		default:
			scan_float();
			break;
	}
}

JsonExpr *parse_expr(void);

char *parse_string(void) {
	if (!is_token(TOKEN_STRING)) {
		// TODO(shaw): better error message
		parse_error("expected string, got %d", token.kind);
		return NULL;
	}
	char *str = _strdup(token.str_val);
	next_token();
	return str;
}

JsonExpr *parse_expr_dict(void) {
	PROFILE_FUNCTION_BEGIN;
	expect_token(TOKEN_LEFT_BRACE);
	BUF(Entry *entries) = NULL;
	do {
		char *key = parse_string();
		expect_token(TOKEN_COLON);
		JsonExpr *val = parse_expr();
		buf_push(entries, (Entry){key, val});
	} while (match_token(TOKEN_COMMA));
	expect_token(TOKEN_RIGHT_BRACE);
	JsonExpr *dict = expr_dict(entries, buf_len(entries));
	PROFILE_FUNCTION_END;
	return dict;
}

JsonExpr *parse_expr_array(void) {
	expect_token(TOKEN_LEFT_BRACKET);
	BUF(JsonExpr **items) = NULL;
	do {
		buf_push(items, parse_expr());
	} while (match_token(TOKEN_COMMA));
	expect_token(TOKEN_RIGHT_BRACKET);
	return expr_array(items, buf_len(items));
}

JsonExpr *parse_expr_string(void) {
	JsonExpr *str = expr_string(token.str_val);
	next_token();
	return str;
}

JsonExpr *parse_expr_float(void) {
	JsonExpr *f = expr_float(token.float_val);
	next_token();
	return f;
}

JsonExpr *parse_expr(void) {
	PROFILE_FUNCTION_BEGIN;
	JsonExpr *expr = NULL;
	switch (token.kind) {
		case TOKEN_LEFT_BRACE:
			expr = parse_expr_dict();
			break;
		case TOKEN_LEFT_BRACKET:
			expr = parse_expr_array();
			break;
		case TOKEN_STRING:
			expr = parse_expr_string();
			break;
		case TOKEN_FLOAT:
			expr = parse_expr_float();
			break;
		default:
			parse_error("Expected dict, array, string, or float");
			break;
	}
	PROFILE_FUNCTION_END;
	return expr;
}

JsonExpr *parse_json(void) {
	PROFILE_FUNCTION_BEGIN;
	JsonExpr *json = NULL;
	if (is_token(TOKEN_LEFT_BRACE)) {
		json = parse_expr_dict();
	} else if (is_token(TOKEN_LEFT_BRACKET)) {
		json = parse_expr_array();
	} else {
		parse_error("expected '{' or '['");
	}
	PROFILE_FUNCTION_END;
	return json;
}

HaversineInput parse_haversine_input(void) {
	PROFILE_FUNCTION_BEGIN;
	HaversineInput input = {0};

	JsonExpr *json = parse_json();

	JsonExpr *pairs = dict_get(json->dict, "pairs");

	input.num_pairs = pairs->array.num_items;
	input.pairs = xcalloc(input.num_pairs, sizeof(Pair));

	for (int i=0; i < pairs->array.num_items; ++i) {
		JsonExpr *item = pairs->array.items[i];
		assert(item->kind == EXPR_DICT);
		Pair *pair = &input.pairs[i];
		JsonExpr *x0 = dict_get(item->dict, "x0");
		if (x0) pair->x0 = x0->float_val;
		JsonExpr *y0 = dict_get(item->dict, "y0");
		if (y0) pair->y0 = y0->float_val;
		JsonExpr *x1 = dict_get(item->dict, "x1");
		if (x1) pair->x1 = x1->float_val;
		JsonExpr *y1 = dict_get(item->dict, "y1");
		if (y1) pair->y1 = y1->float_val;
	}

	PROFILE_FUNCTION_END;
	return input;
}


//------------------------------------------------------------------------------
// from LISTING 65 - Reference Haversine Distance Formula
//------------------------------------------------------------------------------
static F64 square(F64 a) {
    return a*a;
}

static F64 radians_from_degrees(F64 degrees) {
    return 0.01745329251994329577f * degrees;
}

// NOTE(casey): earth_radius is generally expected to be 6372.8
static F64 reference_haversine(F64 x0, F64 y0, F64 x1, F64 y1, F64 earth_radius)
{
    /* NOTE(casey): This is not meant to be a "good" way to calculate the Haversine distance.
       Instead, it attempts to follow, as closely as possible, the formula used in the real-world
       question on which these homework exercises are loosely based.
    */
    
    F64 lat1 = y0;
    F64 lat2 = y1;
    F64 lon1 = x0;
    F64 lon2 = x1;
    
    F64 dLat = radians_from_degrees(lat2 - lat1);
    F64 dLon = radians_from_degrees(lon2 - lon1);
    lat1 = radians_from_degrees(lat1);
    lat2 = radians_from_degrees(lat2);
    
    F64 a = square(sin(dLat/2.0)) + cos(lat1)*cos(lat2)*square(sin(dLon/2));
    F64 c = 2.0*asin(sqrt(a));
    
    F64 result = earth_radius * c;
    
    return result;
}

#define EPSILON 0.00000000001
void validate(char *answers_filepath, HaversineInput input) {
	PROFILE_FUNCTION_BEGIN;
	char *file_data;
	size_t file_size;
	bool ok = read_entire_file(answers_filepath, &file_data, &file_size);
	if (!ok) {
		fprintf(stderr, "error: failed to read file %s\n", answers_filepath);
		exit(1);
	}

	BUF(char *failures) = NULL;

	F64 *distances = (F64*)file_data;
	F64 sum = 0;
	for (size_t i=0; i<input.num_pairs; ++i) {
		Pair pair = input.pairs[i];
		F64 distance = reference_haversine(pair.x0, pair.y0, pair.x1, pair.y1, EARTH_RADIUS_KM);
		sum += distance;
		F64 error = distance - distances[i];
		if (error > EPSILON) {
			buf_printf(failures, "pair %zu: expected %.16f, got %.16f, difference %.16f\n", 
				i, distances[i], distance, error);
		}
	}
	F64 average = input.num_pairs > 0 ? sum / input.num_pairs : 0;
	F64 generated_average = distances[input.num_pairs];

	F64 difference = 0;
	if (generated_average > 0) {
		difference = average - generated_average;
	}

	printf("Number of pairs: %zu\n", input.num_pairs);
	printf("Average haversine distance: %.16f\n", average);
	printf("\nValidation:\n");
	printf("Generated average haversine distance: %.16f\n", generated_average);
	printf("Difference: %.16f\n", difference);
	if (failures) {
		printf("Failures:\n%s\n", failures);
	}

	PROFILE_FUNCTION_END;
}
#undef EPSILON

//------------------------------------------------------------------------------
// Entry Point
//------------------------------------------------------------------------------
int main(int argc, char **argv) {
	begin_profile();

	// setup
	if (argc < 2) {
		printf("Usage: %s [haversine_input.json]\n", argv[0]);
		printf("       %s [haversine_input.json] [answers.f64]\n", argv[0]);
		exit(1);
	}
	char *input_filepath = argv[1];
	char *answers_filepath = argc > 2 ? argv[2] : NULL;

	// reading json input file
	PROFILE_BLOCK_BEGIN("reading json input file");
	char *file_data;
	size_t file_size;
	bool ok = read_entire_file(input_filepath, &file_data, &file_size);
	if (!ok) {
		fprintf(stderr, "error: failed to read file %s\n", input_filepath);
		exit(1);
	}
	PROFILE_BLOCK_END;

	// parsing json
	init_parse(file_data);
	HaversineInput input = parse_haversine_input();

	// computing haversine distances
	if (answers_filepath) {
		validate(answers_filepath, input);
	} else {
		PROFILE_BLOCK_BEGIN("compute haversine");

		F64 sum = 0;
		for (int i=0; i<input.num_pairs; ++i) {
			Pair pair = input.pairs[i];
			F64 distance = reference_haversine(pair.x0, pair.y0, pair.x1, pair.y1, EARTH_RADIUS_KM);
			sum += distance;
		}
		F64 average = input.num_pairs > 0 ? sum / input.num_pairs : 0;

		printf("Number of pairs: %zu\n", input.num_pairs);
		printf("Average haversine distance: %.16f\n", average);

		PROFILE_BLOCK_END;
	}

	end_profile();
	return 0;
}

PROFILE_TRANSLATION_UNIT_END;
