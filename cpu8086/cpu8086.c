#include "../common.c"
#include "instruction_table.c"

#define DISASSEM_BUF_SIZE       4096
#define SINGLE_INST_BUF_SIZE    64
#define OPERAND_BUF_SIZE        32

typedef uint8_t RegIndex;

typedef enum {
	OPERAND_NONE,
	OPERAND_IMM,
	OPERAND_REL_IMM,
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

// NOTE(shaw): maybe collapse this into 1 dimension and use an enum to index it, 
// so we don't use magic register numbers as indices, but then we lose the ability to 
// use the direct register encoding to index into this table (table 4.9 from 8086 manual)
char *regs[2][8] = {
	{ "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" },
	{ "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" },
};

char *effective_address_table[8] = {
	"bx + si", "bx + di", "bp + si", "bp + di",
	"si", "di", "bp", "bx",
};

char *mnemonics[] = {
	[OP_MOV]    = "mov",
	[OP_ADD]    = "add",
	[OP_ADC]    = "adc",
	[OP_SUB]    = "sub",
	[OP_CMP]    = "cmp",
	[OP_JZ]     = "jz",
	[OP_JL]     = "jl",
	[OP_JLE]    = "jle",
	[OP_JB]     = "jb",
	[OP_JBE]    = "jbe",
	[OP_JP]     = "jp",
	[OP_JO]     = "jo",
	[OP_JS]     = "js",
	[OP_JNE]    = "jne",
	[OP_JGE]    = "jge",
	[OP_JG]     = "jg",
	[OP_JNB]    = "jnb",
	[OP_JA]     = "ja",
	[OP_JNP]    = "jnp",
	[OP_JNO]    = "jno",
	[OP_JNS]    = "jns",
	[OP_LOOP]   = "loop",
	[OP_LOOPZ]  = "loopz",
	[OP_LOOPNZ] = "loopnz",
	[OP_JCXZ]   = "jcxz",
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
				} else if (field_values[FIELD_SIGN_EXTEND]) {
					data = (int16_t)(int8_t)data;
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
					uint8_t mask = 0xFF >> (8 - field.num_bits);
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

		if (bits_pending == 0) 
			++byte_index;

		int instruction_size = byte_index;

		inst.wide = field_values[FIELD_WIDE];
		uint8_t mode = field_values[FIELD_MODE];
		bool reg_mode = field_values[FIELD_MODE] == 0x3;
		bool dir = field_values[FIELD_DIR];
		uint8_t r_m = field_values[FIELD_REG_MEM];
		uint16_t disp = field_values[FIELD_DISP];
		uint16_t imm = field_values[FIELD_DATA];

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

		if (field_values[FIELD_SRC_IMM]) {
			if (field_values[FIELD_REL_JMP])
				inst.operands[1] = (Operand) { .kind = OPERAND_REL_IMM, .imm = imm + instruction_size };
			else 
				inst.operands[1] = (Operand) { .kind = OPERAND_IMM, .imm = imm };
		}

		
		stream += instruction_size;

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
	case OPERAND_REL_IMM:
		buf_printf(&buf_ptr, "$+%d", (int16_t)operand->imm);
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
	// NOTE: this is just here during development to point out where you need 
	// to add code when adding ops
	switch (inst->op) {
		case OP_MOV:
		case OP_ADD: case OP_ADC:
		case OP_SUB: case OP_SBB:
		case OP_CMP:
		case OP_JZ: case OP_JL: case OP_JLE: case OP_JB: case OP_JBE: case OP_JP: 
        case OP_JO: case OP_JS: case OP_JNE: case OP_JGE: case OP_JG: case OP_JNB: 
        case OP_JA: case OP_JNP: case OP_JNO: case OP_JNS: case OP_LOOP: case OP_LOOPZ: 
		case OP_LOOPNZ: case OP_JCXZ:
			break;
		default: 
			assert(0);
			break;
	}
	Operand *operand_dst = &inst->operands[0];
	Operand *operand_src = &inst->operands[1];

	char *asm_inst = arena_alloc_zeroed(arena, SINGLE_INST_BUF_SIZE);
	char *buf_ptr = asm_inst;

	char *dst = "";
	char *sep = "";
	if (!(operand_dst->kind == OPERAND_REG && operand_dst->reg == REG_IP)) {
		dst = operand_to_string(arena, operand_dst, inst->wide);
		sep = ", ";
	}

	char *src = operand_to_string(arena, operand_src, inst->wide);

	char *size = "";
	if (operand_src->kind == OPERAND_IMM && operand_dst->kind == OPERAND_MEM) {
		size = inst->wide ? "word" : "byte";
	}

	buf_printf(&buf_ptr, "%s %s%s%s%s", mnemonics[inst->op], size, dst, sep, src);
		
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
