#include "../common.c"
#include "instruction_table.c"

#define DISASSEM_BUF_SIZE       1024
#define SINGLE_INST_BUF_SIZE    64
#define OPERAND_BUF_SIZE        32

/*
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
*/

typedef uint8_t RegIndex;

typedef enum {
	OPERAND_NONE,
	OPERAND_IMM,
	OPERAND_REG,
	OPERAND_MEM,
} OperandKind;

typedef struct {
	RegIndex reg_base;
	RegIndex reg_offset;
	uint16_t imm_offset;
	bool is_direct;
	bool has_reg_offset;
} EffectiveAddress;

typedef struct {
	OperandKind kind;
	union {
		uint16_t imm;
		RegIndex reg;
		EffectiveAddress addr;
	};
} Operand;

typedef struct {
	Operation op;
	Operand operands[2];
	bool wide;
} Instruction;

uint8_t *stream;

uint8_t effective_address_reg_table[2][8] = {
	{ 0x3, 0x3, 0x5, 0x5, 0x6, 0x7, 0x5, 0x3 }, // base reg
	{ 0x6, 0x7, 0x6, 0x7, 0x0, 0x0, 0x0, 0x0 }, // offset reg
};

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

char *reg_name(uint8_t wide, uint8_t reg) {
	return regs[wide][reg];
}

bool is_reg_mode(uint8_t mode) {
	return mode == 0x3;
}

// NOTE(shaw): this isn't great, it doesn't check if there is space in the buffer
// it just writes. it can def be improved but but it works for now
//
// one of the great advantages of this is that we can write code like:
// buf_printf(&buf_ptr, "mov %s, %s", operand_to_string(), operand_to_string());
// and take advantage of format strings rather than having to manually keep
// up with separators and shit like that
// 
// however it is wasteful of memory, because we end up allocating three times
// in the above case, and two of them are redundant. keep in mind though that we 
// are using the super fast arena allocator, so its not slow system allocations.
//
// maybe we should switch to just using fprintf, this will avoid redundant 
// allocations, but then we have to do more manually string manupulation stuff
// idk what is better yet...
void buf_printf(char **buf, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int count = vsprintf(*buf, fmt, args);
	assert(count > 0);
	va_end(args);
	*buf += count;
}

int instruction_count = 0; // JUST USED FOR DEBUGGING
Instruction decode_instruction(void) {
	++instruction_count;

	Instruction inst = { 0 };
	uint16_t field_values[FIELD_COUNT];
	bool instruction_match;
	for (int i = 0; i < ARRAY_COUNT(instruction_table); ++i) {
		memset(&field_values, 0, sizeof(field_values));
		InstructionEncoding inst_encoding = instruction_table[i];
		inst.op = inst_encoding.op;

		instruction_match = true;
		int byte_index = 0;
		uint8_t bits_pending = 8;
	
		for (int j = 0; j < ARRAY_COUNT(inst_encoding.fields); ++j) {
			if (bits_pending == 0) {
				++byte_index;
				bits_pending = 8;
			}

			Field field = inst_encoding.fields[j];

			if (field.kind == FIELD_DISP) {
				uint8_t mode = field_values[FIELD_MODE];
				if ((mode == 0x0 && field_values[FIELD_REG_MEM] == 0x6) || mode == 0x2) {
					// 16 bit displacement
					assert(bits_pending == 8); // ensure disp is byte aligned
					uint16_t lo = stream[byte_index++];
					uint16_t hi = stream[byte_index++];
					field_values[FIELD_DISP] = (hi << 8) | lo;
				} else if (mode == 0x1) {
					// 8 bit displacement
					assert(bits_pending == 8);
					field_values[FIELD_DISP] = stream[byte_index++];
				}

			} else if (field.kind == FIELD_DATA) {
				assert(bits_pending == 8); // ensure immediate is byte aligned
				uint16_t data = stream[byte_index++];
				if (field_values[FIELD_WIDE] && !field_values[FIELD_SIGN_EXTEND]) {
					data |= stream[byte_index++] << 8;
				}
				field_values[FIELD_DATA] = data;

			} else {
				if (field.num_bits > bits_pending) {
					assert(0 && "field crosses byte boundary");
				}

				// read field.num_bits from stream
				uint8_t data;
				if (field.num_bits > 0) {
					data = stream[byte_index] >> (bits_pending - field.num_bits);
					uint8_t mask = ~((int8_t)0x80 >> (7 - field.num_bits));
					data &= mask;
					bits_pending -= field.num_bits;
				} else {
					data = field.value;
				}

				if (field.kind == FIELD_OPCODE) {
					if (data != field.value) {
						instruction_match = false;
						break;
					}
				}

				field_values[field.kind] = data;
			}
		}

		if (!instruction_match) continue;

		inst.wide = field_values[FIELD_WIDE];
		uint8_t mode = field_values[FIELD_MODE];
		bool reg_mode = field_values[FIELD_MODE] == 0x3;
		bool dir = field_values[FIELD_DIR];
		uint8_t r_m = field_values[FIELD_REG_MEM];
		uint16_t disp = field_values[FIELD_DISP];
		uint16_t imm = field_values[FIELD_DATA];
		
		// TODO(shaw): set immediate values in operands

		if (reg_mode) {
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = OPERAND_REG, .reg = field_values[FIELD_REG] };
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = OPERAND_REG, .reg = r_m };
	
		} else {
			// build register operand
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = OPERAND_REG, .reg = field_values[FIELD_REG] };

			// build memory operand
			EffectiveAddress addr;
			addr.is_direct = mode == 0x0 && r_m == 0x6;
			addr.imm_offset = disp;
			if (!addr.is_direct) {
				addr.reg_base = effective_address_reg_table[0][r_m];
				addr.reg_offset = effective_address_reg_table[1][r_m];
				addr.has_reg_offset = r_m < 4;
			}
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = OPERAND_MEM, .addr = addr };
		}

		if (field_values[FIELD_SRC_IS_IMM])
			inst.operands[1] = (Operand){ .kind = OPERAND_IMM, .imm = imm };

		if (bits_pending == 0) 
			++byte_index;
		stream += byte_index;

		return inst;
	}
	assert(0 && "no legal instruction found matching input stream");
	return inst;
}

char *operand_to_string(Arena *arena, Operand *operand, bool wide) {
	if (operand->kind == OPERAND_REG)
		return reg_name(wide, operand->reg);
	
	char *str = arena_alloc_zeroed(arena, OPERAND_BUF_SIZE);
	char *buf_ptr = str;

	switch (operand->kind) {
	case OPERAND_IMM:
		buf_printf(&buf_ptr, "%d", operand->imm);
		break;
	case OPERAND_MEM: {
		EffectiveAddress addr = operand->addr;
		if (addr.is_direct) {
			buf_printf(&buf_ptr, "[%d]", addr.imm_offset);
		} else {
			buf_printf(&buf_ptr, "[%s", reg_name(1, addr.reg_base));
			if (addr.has_reg_offset)
				buf_printf(&buf_ptr, " + %s", reg_name(1, addr.reg_offset));
			if (addr.imm_offset)
				buf_printf(&buf_ptr, " + %d", addr.imm_offset);
			buf_printf(&buf_ptr, "]");
		}
		break;
	}
	default:
		assert(0);
		break;
	}

	return str;
}

char *disassemble_instruction(Arena *arena, Instruction *inst) {
	assert(inst->op == OP_MOV);

	char *asm_inst = arena_alloc_zeroed(arena, SINGLE_INST_BUF_SIZE);
	char *buf_ptr = asm_inst;

	char *dst = operand_to_string(arena, &inst->operands[0], inst->wide);
	char *src = operand_to_string(arena, &inst->operands[1], inst->wide);

	buf_printf(&buf_ptr, "mov %s, %s", dst, src);
		

	return asm_inst;
}
	
/*
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
	*/

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
