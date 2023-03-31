#include "..\common.c"

#define DISASSEM_BUF_SIZE 1024

typedef struct {
	uint16_t opcode : 6;
	uint16_t dir    : 1;
	uint16_t wide   : 1;
	uint16_t mode   : 2;
	uint16_t reg    : 3;
	uint16_t r_m    : 3;
} Instruction;

void buf_printf(char **buf, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int count = vsprintf(*buf, fmt, args);
	assert(count > 0);
	va_end(args);
	*buf += count;
}

char *disassemble_mov_instruction(Arena *arena, Instruction inst) {
	char *regs[2][8] = {
		{ "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" },
		{ "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" },
	};
	
	uint8_t src = inst.reg;
	uint8_t dst = inst.r_m;
	if (inst.dir == 1) {
		src = inst.r_m;
		dst = inst.reg;
	}

	char buf[11];
	snprintf(buf, 11, "mov %s, %s", regs[inst.wide][dst], regs[inst.wide][src]);

	char *asm_inst = arena_alloc(arena, 11);
	strncpy(asm_inst, buf, 11);
	asm_inst[11] = 0;
	return asm_inst;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s <filepath>\n", argv[0]);
		exit(1);
	}

	FILE *fp = fopen(argv[1], "rb");
	if (!fp) {
		perror("fopen");
		exit(1);
	}

	char *file_data;
	size_t file_size;
	int rc = read_entire_file(fp, &file_data, &file_size);
	if (rc != READ_ENTIRE_FILE_OK) {
		fprintf(stderr, "Failed to read file %s\n", argv[1]);
		exit(1);
	}

	Arena string_arena = {0};
	char buf[DISASSEM_BUF_SIZE];
	char *buf_ptr = buf;

	buf_printf(&buf_ptr, "bits 16\n\n");

	for (char *cursor = file_data; cursor < file_data + file_size; cursor += 2) {
		Instruction inst = {0};
		inst.opcode = (cursor[0] & 0xFC) >> 2;
		inst.dir    = (cursor[0] >> 1) & 1;
		inst.wide   = cursor[0] & 1;
		inst.mode   = (cursor[1] >> 6) & 0x3;
		inst.reg    = (cursor[1] >> 3) & 0x7;
		inst.r_m    = (cursor[1] >> 0) & 0x7;
		char *asm_inst = disassemble_mov_instruction(&string_arena, inst);
		buf_printf(&buf_ptr, "%s\n", asm_inst);
	}
	fclose(fp);
	*buf_ptr = 0;
	
	fp = fopen("test.asm", "w");
	if (!fp) {
		perror("fopen");
		exit(1);
	}

	size_t count = buf_ptr - buf;
	if (fwrite(buf, 1, buf_ptr - buf, fp) < count) {
		perror("fwrite");
		fclose(fp);
		exit(1);
	}

	fclose(fp);
	return 0;
}