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

char *arithmetic_ops[] = {
	[0x0] = "add",
	[0x5] = "sub",
	[0x7] = "cmp",
};

char *conditional_jump_names[] = {
	[0x70] = "jo",     [0x71] = "jno",   [0x72] = "jb",   [0x73] = "jnb",
	[0x74] = "je",     [0x75] = "jne",   [0x76] = "jbe",  [0x77] = "ja",
	[0x78] = "js",     [0x79] = "jns",   [0x7A] = "jp",   [0x7B] = "jnp",
	[0x7C] = "jl",     [0x7D] = "jnl",   [0x7E] = "jle",  [0x7F] = "jg",
	[0xE0] = "loopnz", [0xE1] = "loopz", [0xE2] = "loop", [0xE3] = "jcxz",
};

uint8_t *stream;

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
		inst->disp = stream[2];
		disp_bytes = 1;
	} else if (inst->mode == 0x2 || (inst->mode == 0x0 && inst->r_m == 0x6)) {
		inst->disp = ((uint16_t)stream[3] << 8) | stream[2];
		disp_bytes = 2;
	} else {
		assert(inst->mode == 0x0 || inst->mode == 0x3);
	}
	return disp_bytes;
}

bool is_arithmetic(Instruction *inst) {
	// HACK: this only works because were currently only considering mov and arithmtic ops
	// and unconditional jumps, but that just happens not to interfere
	return inst->opcode < 0x22;
}

bool imm_to_rm(Instruction *inst) {
	return inst->opcode == 0x31 || inst->opcode == 0x20;
}

bool rm_to_from_reg(Instruction *inst) {
	return inst->opcode == 0x22 || (inst->opcode & 0x31) == 0x0;
}

bool imm_to_acc(Instruction *inst) {
	return (inst->opcode & 0x31) == 0x01;
}

// lo and hi are offsets of the low and high immediate byte, from the first byte of the instruction
int decode_immediate(Instruction *inst, int lo, int hi) {
	int data_bytes = 1;
	inst->data = stream[lo];

	bool arithemtic_imm_rm = is_arithmetic(inst) && imm_to_rm(inst);

	if ((arithemtic_imm_rm && inst->wide && !inst->dir) ||
		(!arithemtic_imm_rm && inst->wide)) 
	{
		inst->data |= (uint16_t)stream[hi] << 8;
		data_bytes = 2;
	}

	return data_bytes;
}

bool is_conditional_jump(uint8_t opcode) {
	return (opcode > 0x6F && opcode < 0x80) ||
	       (opcode > 0xDF && opcode < 0xE4);
}

Instruction decode_instruction(void) {
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

	// mov immediate to register
	if ((stream[0] & 0xF0) == 0xB0) {
		inst.wide = (stream[0] >> 3) & 1;
		inst.reg = stream[0] & 0x7; 
		// NOTE(shaw): the minus 1 here is because in immediate to register 
		// the lo data byte clobbers the mod reg r/m byte used by other instructions
		data_bytes = decode_immediate(&inst, 1, 2) - 1;
		
	} else if (rm_to_from_reg(&inst)) {
		disp_bytes = decode_displacement(&inst);

	} else if (imm_to_rm(&inst)) {
		disp_bytes = decode_displacement(&inst);
		data_bytes = decode_immediate(&inst, 2+disp_bytes, 3+disp_bytes);

	} else if (imm_to_acc(&inst)) {
		// NOTE(shaw): the minus 1 here is because in immediate to accumulator 
		// the lo data byte clobbers the mod reg r/m byte used by other instructions
		data_bytes = decode_immediate(&inst, 1, 2) - 1;
	
	// mov memory to accumulator & vice versa
	} else if (inst.opcode == 0x28) {
		inst.addr = ((uint16_t)stream[2] << 8) | stream[1];
		addr_bytes = 2;

	// mov register/memory to segment register & vice versa
	} else if (inst.opcode == 0x23) {
		disp_bytes = decode_displacement(&inst);

	} else if (is_conditional_jump(stream[0])) {
		inst.disp = stream[1];
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

	if (imm_to_rm(inst) && !is_reg_mode(inst->mode)) {
		buf_printf(&buf_ptr, "%s ", inst->wide ? "word" : "byte");
	}

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

char *disassemble_conditional_jump(Arena *arena, Instruction *inst) {
	// FIXME: have to recompute the full opcode since we are only storing 6 bits of opcode directly
	// should probably store 8 bit opcode all the time, but there are a lot of places where the 6 
	// bit version is used with bitmasks and will need to go change all of those. im lazy rn
	uint8_t full_opcode = ((uint8_t)inst->opcode << 2) | inst->dir << 1 | inst->wide;
	int8_t displacement = (int8_t)inst->disp;
	
	// longest conditional jump is 6 chars + space + largest 8bit signed is 4 chars
	int len = 16; 
	char *asm_inst = arena_alloc_zeroed(arena, len);
	snprintf(asm_inst, len, "%s %d", conditional_jump_names[full_opcode], displacement);
	return asm_inst;
}

char *disassemble_instruction(Arena *arena, Instruction *inst) {
	uint8_t full_opcode = ((uint8_t)inst->opcode << 2) | inst->dir << 1 | inst->wide;
	if (is_conditional_jump(full_opcode)) {
		return disassemble_conditional_jump(arena, inst);
	}

	char *src, *dst;

	// mov immediate to register
	if ((inst->opcode & 0x3C) == 0x2C) {
		// NOTE: max immediate is 65536, so 8 is enough chars
		src = arena_alloc_zeroed(arena, 8);
		snprintf(src, 8, "%d", inst->data);
		dst = reg_name(inst->wide, inst->reg);

	// register/memory to/from register
	} else if (rm_to_from_reg(inst)) {
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
	} else if (imm_to_rm(inst)) {
		// NOTE: max immediate is 65536, so 8 is enough chars
		src = arena_alloc_zeroed(arena, 8);
		snprintf(src, 8, "%d", inst->data);
		dst = is_reg_mode(inst->mode)
				? reg_name(inst->wide, inst->r_m)
				: effective_address(arena, inst);

	// immediate to accumulator
	} else if (imm_to_acc(inst)) {
		dst = reg_name(inst->wide, 0); // accumulator
		src = arena_alloc_zeroed(arena, 8);
		snprintf(src, 8, "%d", inst->data);

	// memory to accumulator & vice versa
	} else if (inst->opcode == 0x28) {
		assert(0);
	// register/memory to segment register & vice versa
	} else if (inst->opcode == 0x23) {
		assert(0);
	} 
	
	int len = 4 + strlen(dst) + 2 + strlen(src) + 1;
	char *asm_inst = arena_alloc(arena, len);

	if (is_arithmetic(inst)) {
		int op = imm_to_rm(inst) 
			? inst->reg 
			: (inst->opcode >> 1) & 0x7;
		snprintf(asm_inst, len, "%s %s, %s", arithmetic_ops[op], dst, src);
	}
	else
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

	stream = (uint8_t*)file_data;

	while (stream < file_data + file_size) {
		Instruction inst = decode_instruction();
		char *asm_inst = disassemble_instruction(&string_arena, &inst);
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
