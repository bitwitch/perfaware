#include "../common.c"
#include "cpu8086.h"
#include "instruction_table.c"

uint8_t memory[1 * MB];
uint16_t regs[REG_COUNT];

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

char *reg_name(Register reg) {
	// NOTE(shaw): maybe memory allocation with string interning would be a good fit??
	// rather than all these tables??

	static char *half_reg_names[2][4] = {
		{ "al", "bl", "cl", "dl" },
		{ "ah", "bh", "ch", "dh" }
	};

	static char *reg_names[9] = {
		[REG_A]  = "ax",
		[REG_B]  = "bx",
		[REG_C]  = "cx",
		[REG_D]  = "dx",
		[REG_SI] = "si",
		[REG_DI] = "di",
		[REG_SP] = "sp",
		[REG_BP] = "bp",
		[REG_IP] = "ip",
	};

	if (reg.size == 1) {
		assert(reg.index < 4);
		return half_reg_names[reg.offset][reg.index];
	} else {
		assert(reg.size == 2);
		return reg_names[reg.index];
	}
}

uint32_t absolute_address(EffectiveAddress *eff_addr) {
	if (eff_addr->is_direct)
		return eff_addr->imm_offset;

	uint32_t offset = regs[eff_addr->reg_base.index];
	if (eff_addr->has_reg_offset) 
		offset += regs[eff_addr->reg_offset.index];
	offset += eff_addr->imm_offset;
	assert(offset <= 1*MB);
	return offset;
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

Register reg_from_encoding(uint8_t encoding, bool wide) {
	static Register reg_encoding_table[9][2] = {
		{ { REG_A,  1, 0 }, { REG_A,  2, 0 } },
		{ { REG_C,  1, 0 }, { REG_C,  2, 0 } },
		{ { REG_D,  1, 0 }, { REG_D,  2, 0 } },
		{ { REG_B,  1, 0 }, { REG_B,  2, 0 } },
		{ { REG_A,  1, 1 }, { REG_SP, 2, 0 } },
		{ { REG_C,  1, 1 }, { REG_BP, 2, 0 } },
		{ { REG_D,  1, 1 }, { REG_SI, 2, 0 } },
		{ { REG_B,  1, 1 }, { REG_DI, 2, 0 } },
		{ { REG_IP, 2, 0 }, { REG_IP, 2, 0 } }
	};
	return reg_encoding_table[encoding][wide];
}

EffectiveAddress effective_address_from_encoding(uint8_t mode, uint8_t r_m, uint16_t disp) {
	static Register reg_encoding_table[8] = {
		{ REG_B,  2, 0 }, { REG_B,  2, 0 }, { REG_BP, 2, 0 }, { REG_BP, 2, 0 },
		{ REG_SI, 2, 0 }, { REG_DI, 2, 0 }, { REG_BP, 2, 0 }, { REG_B,  2, 0 }
	};

	EffectiveAddress addr = {0};
	addr.is_direct = mode == 0x0 && r_m == 0x6;
	addr.imm_offset = disp;
	if (!addr.is_direct) {
		addr.reg_base = reg_encoding_table[r_m];
		if (r_m < 4) {
			addr.reg_offset = (r_m % 2) == 0 ? (Register){REG_SI, 2, 0} : (Register){REG_DI, 2, 0};
			addr.has_reg_offset = true;
		}
	}
	return addr;
}

int instruction_count = 0; // JUST USED FOR DEBUGGING
Instruction decode_instruction(void) {
	++instruction_count;

	uint8_t *stream = &memory[regs[REG_IP]];

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

		inst.wide      = field_values[FIELD_WIDE];
		uint8_t reg    = field_values[FIELD_REG];
		uint8_t mode   = field_values[FIELD_MODE];
		bool reg_mode  = field_values[FIELD_MODE] == 0x3;
		bool dir       = field_values[FIELD_DIR];
		uint8_t r_m    = field_values[FIELD_REG_MEM];
		uint16_t disp  = field_values[FIELD_DISP];
		uint16_t imm   = field_values[FIELD_DATA];

		if (reg_mode) {
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = OPERAND_REG, .reg = reg_from_encoding(reg, inst.wide) };
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = OPERAND_REG, .reg = reg_from_encoding(r_m, inst.wide) };

		} else {
			// build register operand
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = OPERAND_REG, .reg = reg_from_encoding(reg, inst.wide) };
			// build memory operand
			EffectiveAddress addr = effective_address_from_encoding(mode, r_m, disp);
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = OPERAND_MEM, .addr = addr };
		}

		if (field_values[FIELD_SRC_IMM]) {
			if (field_values[FIELD_REL_JMP])
				inst.operands[1] = (Operand) { .kind = OPERAND_REL_IMM, .imm = imm + instruction_size };
			else 
				inst.operands[1] = (Operand) { .kind = OPERAND_IMM, .imm = imm };
		}

		regs[REG_IP] += instruction_size;

		return inst;
	}
	assert(0 && "no legal instruction found matching input stream");
	return inst;
}

void execute_instruction(Instruction *inst) {
	assert(inst->op == OP_MOV);
	assert(inst->wide);
	Operand *operand_dst = &inst->operands[0];
	Operand *operand_src = &inst->operands[1];

	uint16_t *dst;
	if (operand_dst->kind == OPERAND_REG) {
		dst = &regs[operand_dst->reg.index];
	} else {
		assert(operand_dst->kind == OPERAND_MEM);
		dst = (uint16_t*) &memory[absolute_address(&operand_dst->addr)];
	}

	uint16_t src;
	if (operand_src->kind == OPERAND_REG) {
		src = regs[operand_src->reg.index];
	} else if (operand_src->kind == OPERAND_MEM) {
		src = memory[absolute_address(&operand_src->addr)];
	} else {
		assert(operand_src->kind == OPERAND_IMM);
		src = operand_src->imm;
	}

	*dst = src;
}


char *operand_to_string(Arena *arena, Operand *operand, bool wide) {
	if (operand->kind == OPERAND_REG)
		return reg_name(operand->reg);
	
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
			buf_printf(&buf_ptr, "[%s", reg_name(addr.reg_base));
			if (addr.has_reg_offset)
				buf_printf(&buf_ptr, " + %s", reg_name(addr.reg_offset));
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
	if (!(operand_dst->kind == OPERAND_REG && operand_dst->reg.index == REG_IP)) {
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

	// TODO(shaw): read file directly into memory buffer to avoid the copy
	char *file_data;
	size_t file_size;
	int rc = read_entire_file(fp, &file_data, &file_size);
	if (rc != READ_ENTIRE_FILE_OK) {
		fprintf(stderr, "Failed to read file %s\n", argv[1]);
		exit(1);
	}
	assert(file_size <= 1*MB);
	memcpy(memory, file_data, file_size);

	Arena string_arena = {0};
	char buf[DISASSEM_BUF_SIZE];
	char *buf_ptr = buf;

	buf_printf(&buf_ptr, "bits 16\n");

	while (regs[REG_IP] < file_size) {
		Instruction inst = decode_instruction();
		//execute_instruction(&inst);
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
