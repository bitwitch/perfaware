#include "../common.c"

#define DISASSEM_BUF_SIZE       1024
#define SINGLE_INST_BUF_SIZE    64
#define OPERAND_BUF_SIZE        32


typedef struct {
	uint16_t opcode : 6;
	uint16_t dir    : 1;
	uint16_t wide   : 1;
	uint16_t mode   : 2;
	uint16_t reg    : 3;
	uint16_t r_m    : 3;

	uint16_t disp; // displacement
	uint16_t data; // immediate
	uint16_t addr; // memory address
} Instruction;

char *regs[2][8] = {
	{ "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" },
	{ "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" },
};
char *effective_address_table[8] = {
	"bx + si", "bx + di", "bp + si", "bp + di",
	"si", "di", "bp", "bx",
};
char *stream;

char *reg_name(uint8_t wide, uint8_t reg) {
	return regs[wide][reg];
}

bool is_reg_mode(uint8_t mode) {
	return mode == 0x3;
}

// NOTE(shaw): this isn't great, it doesn't check if there is space in the buffer
// it just writes. but it works for quick and dirty code
void buf_printf(char **buf, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int count = vsprintf(*buf, fmt, args);
	assert(count > 0);
	va_end(args);
	*buf += count;
}

int decode_displacement(Instruction *inst) {
	int disp_bytes = 0;
	if (inst->mode == 0x1) {
		inst->disp = stream[2] & 0xFF;
		disp_bytes = 1;
	} else if (inst->mode == 0x2 || (inst->mode == 0x0 && inst->r_m == 0x6)) {
		uint16_t lo = stream[2] & 0xFF;
		uint16_t hi = stream[3] & 0xFF;
		inst->disp = (hi << 8) | lo;
		disp_bytes = 2;
	} else {
		assert(inst->mode == 0x0 || inst->mode == 0x3);
	}
	return disp_bytes;
}

Instruction decode_mov_instruction(void) {
	Instruction inst = {
		.opcode = (stream[0] & 0xFC) >> 2,
		.dir    = (stream[0] >> 1) & 1,
		.wide   = stream[0] & 1,
		.mode   = (stream[1] >> 6) & 0x3,
		.reg    = (stream[1] >> 3) & 0x7,
		.r_m    = (stream[1] >> 0) & 0x7,
	};

	int disp_bytes = 0;
	int data_bytes = 0;
	int addr_bytes = 0;

	// immediate to register
	if ((stream[0] & 0xB0) == 0xB0) {
		inst.wide = (stream[0] >> 3) & 1;
		inst.reg = stream[0] & 0x7; 
		inst.data = stream[1] & 0xFF;
		if (inst.wide) { 
			uint16_t hi = stream[2] & 0xFF;
			inst.data |= hi << 8;
			data_bytes = 1;
			// NOTE(shaw): data_bytes is only 1 here, because in immediate to register 
			// the lo data byte clobbers the mod reg r/m byte used by other instructions
		}

	// register/memory to/from register
	} else if (inst.opcode == 0x22) {
		disp_bytes = decode_displacement(&inst);

	// immediate to register/memory
	} else if (inst.opcode == 0x31) {
		disp_bytes = decode_displacement(&inst);
		inst.data = stream[4] & 0xFF;
		data_bytes = 1;
		if (inst.wide) { 
			uint16_t hi = stream[5] & 0xFF;
			inst.data |= hi << 8;
			data_bytes = 2;
		}
	
	// memory to accumulator & vice versa
	} else if (inst.opcode == 0x28) {
		inst.addr = (stream[2] << 8) | stream[1];
		addr_bytes = 2;

	// register/memory to segment register & vice versa
	} else if (inst.opcode == 0x23) {
		disp_bytes = decode_displacement(&inst);

	} else {
		assert(0);
	}

	stream += 2 + disp_bytes + data_bytes + addr_bytes;

	return inst;
}

bool is_direct_address(Instruction *inst) {
	return inst->mode == 0x0 && inst->r_m == 0x6;
}

char *effective_address(Arena *arena, Instruction *inst) {
	char *addr = arena_alloc_zeroed(arena, OPERAND_BUF_SIZE);
	char *buf_ptr = addr;

	if (is_direct_address(inst)) {
		buf_printf(&buf_ptr, "[%d]", inst->disp);
	} else {
		buf_printf(&buf_ptr, "[%s", effective_address_table[inst->r_m]);

		if (inst->mode == 0) {
			buf_printf(&buf_ptr, "]");
		} else  {
			assert(inst->mode == 1 || inst->mode == 2);
			buf_printf(&buf_ptr, " + %d]", inst->disp);
		} 
	}

	return addr;
}

char *disassemble_mov_instruction(Arena *arena, Instruction *inst) {
	char *src, *dst;

	// immediate to register
	if ((inst->opcode & 0x2C) == 0x2C) {
		// NOTE: max immediate is 65536, so 8 is enough chars
		src = arena_alloc_zeroed(arena, 8);
		snprintf(src, 8, "%d", inst->data);
		dst = reg_name(inst->wide, inst->reg);

	// register/memory to/from register
	} else if (inst->opcode == 0x22) {
		if (inst->dir == 0) {
			// src is reg, dst is r/m
			src = reg_name(inst->wide, inst->reg);
			dst = is_reg_mode(inst->mode)
					? reg_name(inst->wide, inst->r_m)
					: effective_address(arena, inst);
		} else {
			// src is r/m, dst is reg
			src = is_reg_mode(inst->mode)
					? reg_name(inst->wide, inst->r_m)
					: effective_address(arena, inst);
			dst = reg_name(inst->wide, inst->reg);
		}

	// immediate to register/memory
	} else if (inst->opcode == 0x31) {
		// NOTE: max immediate is 65536, so 8 is enough chars
		src = arena_alloc_zeroed(arena, 8);
		snprintf(src, 8, "%d", inst->data);
		dst = is_reg_mode(inst->mode)
				? reg_name(inst->wide, inst->r_m)
				: effective_address(arena, inst);

	// memory to accumulator & vice versa
	} else if (inst->opcode == 0x28) {
		assert(0);
	// register/memory to segment register & vice versa
	} else if (inst->opcode == 0x23) {
		assert(0);
	} else {
		assert(0);
	}
	
	int len = 4 + strlen(dst) + 2 + strlen(src) + 1;
	char *asm_inst = arena_alloc(arena, len);
	snprintf(asm_inst, len, "mov %s, %s", dst, src);
	asm_inst[len] = 0;
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

	buf_printf(&buf_ptr, "bits 16\n");

	stream = file_data;

	while (stream < file_data + file_size) {
		Instruction inst = decode_mov_instruction();
		char *asm_inst = disassemble_mov_instruction(&string_arena, &inst);
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
