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
	[OP_JNZ]    = "jnz",
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

char *half_reg_names[2][4] = {
	{ "al", "bl", "cl", "dl" },
	{ "ah", "bh", "ch", "dh" }
};

char *reg_names[] = {
	[REG_A]     = "ax",
	[REG_B]     = "bx",
	[REG_C]     = "cx",
	[REG_D]     = "dx",
	[REG_SI]    = "si",
	[REG_DI]    = "di",
	[REG_SP]    = "sp",
	[REG_BP]    = "bp",
	[REG_ES]    = "es",
	[REG_CS]    = "cs",
	[REG_SS]    = "ss",
	[REG_DS]    = "ds",
	[REG_FLAGS] = "flags",
	[REG_IP]    = "ip",
};

char *reg_name(Register reg) {
	// NOTE(shaw): maybe memory allocation with string interning would be a good fit??
	// rather than all these tables??

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

void set_flag(Flag flag, bool val) {
	if (val)
		regs[REG_FLAGS] = regs[REG_FLAGS] | flag;
	else
		regs[REG_FLAGS] = regs[REG_FLAGS] & ~flag;
}

bool get_flag(Flag flag) {
	return regs[REG_FLAGS] & flag;
}

void dump_registers(void) {
	for (int i = 0; i < REG_COUNT; ++i) {
		if (i == REG_FLAGS) {
			printf("%-5s:  ", reg_names[i]);
			if (get_flag(FLAG_ZERO)) printf("Z");
			if (get_flag(FLAG_SIGN)) printf("S");
			printf("\n");
		} else {
			printf("%-5s:  0x%04X (%d)\n", reg_names[i], regs[i], regs[i]);
		}
	}
}

void dump_memory_to_file(void) {
	FILE *fp = fopen("memory_dump.data", "wb");
	if (!fp) {
		perror("fopen");
		exit(1);
	}

	size_t count = 1 * MB;
	if (fwrite(memory, 1, count, fp) < count) {
		perror("fwrite");
		fclose(fp);
		exit(1);
	}
	fclose(fp);
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

Register seg_reg_from_encoding(uint8_t encoding) {
	static Register reg_encoding_table[24] = {
		{ REG_ES, 2, 0 },
		{ REG_CS, 2, 0 },
		{ REG_SS, 2, 0 },
		{ REG_DS, 2, 0 }
	};
	return reg_encoding_table[encoding];
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

Instruction decode_instruction(void) {
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

		inst.wide        = field_values[FIELD_WIDE];
		uint8_t reg      = field_values[FIELD_REG];
		uint8_t sr       = field_values[FIELD_SEG_REG];
		bool has_seg_reg = field_values[FIELD_HAS_SEG_REG];
		uint8_t mode     = field_values[FIELD_MODE];
		bool reg_mode    = field_values[FIELD_MODE] == 0x3;
		bool dir         = field_values[FIELD_DIR];
		uint8_t r_m      = field_values[FIELD_REG_MEM];
		uint16_t disp    = field_values[FIELD_DISP];
		uint16_t imm     = field_values[FIELD_DATA];

		if (reg_mode) {
			Register r = reg_from_encoding(reg, inst.wide);
			OperandKind kind = (r.index == REG_A) ? OPERAND_ACC : OPERAND_REG;
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = kind, .reg = r };

			r = reg_from_encoding(r_m, inst.wide);
			kind = (r.index == REG_A) ? OPERAND_ACC : OPERAND_REG;
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = kind, .reg = r };

		} else {
			// build register operand
			Register r = reg_from_encoding(reg, inst.wide);
			OperandKind kind = (r.index == REG_A) ? OPERAND_ACC : OPERAND_REG;
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = kind, .reg = r };
			// build memory operand
			EffectiveAddress addr = effective_address_from_encoding(mode, r_m, disp);
			inst.operands[dir ? 1 : 0] = (Operand) { .kind = OPERAND_MEM, .addr = addr };
		}

		if (field_values[FIELD_SRC_IMM]) {
			if (field_values[FIELD_REL_JMP])
				inst.operands[1] = (Operand) { .kind = OPERAND_REL_IMM, .imm = imm };
			else 
				inst.operands[1] = (Operand) { .kind = OPERAND_IMM, .imm = imm };
		}

		if (has_seg_reg) {
			inst.operands[dir ? 0 : 1] = (Operand) { .kind = OPERAND_SEG_REG, .reg = seg_reg_from_encoding(sr) };
		}

		regs[REG_IP] += instruction_size;

		return inst;
	}
	assert(0 && "no legal instruction found matching input stream");
	return inst;
}

void execute_op_wide(Operation op, uint16_t *dst, uint16_t val) {
	switch (op) {
		case OP_MOV: 
			*dst = val;
			break;
		case OP_ADD: 
			*dst += val;
			set_flag(FLAG_ZERO, *dst == 0);
			set_flag(FLAG_SIGN, (*dst >> 15) & 1);
			break;
		case OP_SUB:
			*dst -= val;
			set_flag(FLAG_ZERO, *dst == 0);
			set_flag(FLAG_SIGN, (*dst >> 15) & 1);
			break;
		case OP_CMP: {
			uint16_t result = *dst - val;
			set_flag(FLAG_ZERO, result == 0);
			set_flag(FLAG_SIGN, (result >> 15) & 1);
			break;
		}
		case OP_JNZ: 
			if (!get_flag(FLAG_ZERO))
				regs[REG_IP] += (int16_t)val;
			break;
		case OP_LOOP: 
			if (--regs[REG_C] != 0)
				regs[REG_IP] += (int16_t)val;
			break;

		default: 
			assert(0);
			break;
	}
}

void execute_op_byte(Operation op, uint8_t *dst, uint8_t val) {
	switch (op) {
		case OP_MOV: 
			*dst = val;
			break;
		case OP_ADD: 
			*dst += val;
			set_flag(FLAG_ZERO, *dst == 0);
			set_flag(FLAG_SIGN, (*dst >> 7) & 1);
			break;
		case OP_SUB:
			*dst -= val;
			set_flag(FLAG_ZERO, *dst == 0);
			set_flag(FLAG_SIGN, (*dst >> 7) & 1);
			break;
		case OP_CMP: {
			uint8_t result = *dst - val;
			set_flag(FLAG_ZERO, result == 0);
			set_flag(FLAG_SIGN, (result >> 7) & 1);
			break;
		}
		default: 
			assert(0);
			break;
	}
	
}

bool operand_is_reg(OperandKind kind) {
	return kind == OPERAND_REG || kind == OPERAND_ACC || kind == OPERAND_SEG_REG;
}

void execute_instruction(Instruction *inst) {
	Operand *operand_dst = &inst->operands[0];
	Operand *operand_src = &inst->operands[1];
	
	uint16_t *dst;
	if (operand_is_reg(operand_dst->kind)) {
		Register reg = operand_dst->reg;
		// NOTE(shaw): first cast to uint8_t* so that the offset only shifts the address by one byte
		dst = (uint16_t*)((uint8_t*) &regs[reg.index] + reg.offset);
	} else {
		assert(operand_dst->kind == OPERAND_MEM);
		dst = (uint16_t*) &memory[absolute_address(&operand_dst->addr)];
	}

	uint16_t src;
	if (operand_is_reg(operand_src->kind)) {
		Register reg = operand_src->reg;
		src = regs[reg.index] >> (8 * reg.offset);
	} else if (operand_src->kind == OPERAND_MEM) {
		uint32_t addr = absolute_address(&operand_src->addr);
		src = memory[addr] | (memory[addr + 1] << 8);
	} else {
		assert(operand_src->kind == OPERAND_IMM || operand_src->kind == OPERAND_REL_IMM);
		src = operand_src->imm;
	}

	if (inst->wide) {
		execute_op_wide(inst->op, dst, src);
	} else {
		execute_op_byte(inst->op, (uint8_t*)dst, src & 0xFF);
	}
}


char *operand_to_string(Arena *arena, Operand *operand) {
	if (operand_is_reg(operand->kind)) 
		return reg_name(operand->reg);
	
	BUF(char *operand_buf) = NULL;

	switch (operand->kind) {
	case OPERAND_IMM:
		buf_printf(operand_buf, "%d", operand->imm);
		break;
	case OPERAND_REL_IMM:
		// HACK: this is assumming all OPERAND_REL_IMM are operands to conditional jumps
		// and the +2 here is just because the nasm assembler uses a syntax where the immediate 
		// offset is from the start of the instruction, whereas every other fucking thing is 
		// relative to the end of the instruction
		buf_printf(operand_buf, "$+%d", (int16_t)operand->imm + 2);
		break;
	case OPERAND_MEM: {
		EffectiveAddress addr = operand->addr;
		if (addr.is_direct) {
			buf_printf(operand_buf, "[%d]", addr.imm_offset);
		} else {
			buf_printf(operand_buf, "[%s", reg_name(addr.reg_base));
			if (addr.has_reg_offset)
				buf_printf(operand_buf, " + %s", reg_name(addr.reg_offset));
			if (addr.imm_offset)
				buf_printf(operand_buf, " + %d", addr.imm_offset);
			buf_printf(operand_buf, "]");
		}
		break;
	}
	default:
		assert(0);
		break;
	}

	return operand_buf;
}

int calculate_ea_clocks(EffectiveAddress *ea) {
	if (ea->is_direct)
		return 6;
	if (!ea->has_reg_offset && !ea->imm_offset)
		return 5;
	if (!ea->has_reg_offset && ea->imm_offset)
		return 9;
	if (ea->has_reg_offset && !ea->imm_offset) {
		if (ea->reg_base.index == REG_BP && ea->reg_offset.index == REG_DI ||
			ea->reg_base.index == REG_B  && ea->reg_offset.index == REG_SI) 
		{
			return 7;
		} 
		else
			return 8;
	}

	// if displacement + base + index
	{
		if (ea->reg_base.index == REG_BP && ea->reg_offset.index == REG_DI ||
			ea->reg_base.index == REG_B  && ea->reg_offset.index == REG_SI)
		{
			return 11;
		}
		else
			return 12;
	}
}

int total_clocks;
char *disassemble_instruction(Arena *arena, Instruction *inst) {
	// NOTE: this is just here during development to point out where you need 
	// to add code when adding ops
	switch (inst->op) {
		case OP_MOV:
		case OP_ADD: case OP_ADC:
		case OP_SUB: case OP_SBB:
		case OP_CMP:
		case OP_JZ: case OP_JL: case OP_JLE: case OP_JB: case OP_JBE: case OP_JP: 
        case OP_JO: case OP_JS: case OP_JNZ: case OP_JGE: case OP_JG: case OP_JNB: 
        case OP_JA: case OP_JNP: case OP_JNO: case OP_JNS: case OP_LOOP: case OP_LOOPZ: 
		case OP_LOOPNZ: case OP_JCXZ:
			break;
		default: 
			assert(0);
			break;
	}
	Operand *operand_dst = &inst->operands[0];
	Operand *operand_src = &inst->operands[1];

	BUF(char *asm_inst) = NULL;

	char *dst = "";
	char *sep = "";
	if (!(operand_dst->kind == OPERAND_REG && operand_dst->reg.index == REG_IP)) {
		dst = operand_to_string(arena, operand_dst);
		sep = ", ";
	}

	char *src = operand_to_string(arena, operand_src);

	char *size = "";
	if (operand_src->kind == OPERAND_IMM && operand_dst->kind == OPERAND_MEM) {
		size = inst->wide ? "word" : "byte";
	}

	int inst_clocks = 0;
	for (int i=0; i<INSTRUCTION_CLOCKS_TABLE_MAX_LIST; ++i) {
		InstructionClocksEntry entry = instruction_clocks_table[inst->op][i];
		if (entry.dst == operand_dst->kind && entry.src == operand_src->kind) {
			inst_clocks = entry.clocks;	
			if (entry.add_ea_clocks) {
				Operand *operand_mem = (operand_dst->kind == OPERAND_MEM) ? operand_dst : operand_src;
				inst_clocks += calculate_ea_clocks(&operand_mem->addr);
			}
			break;
		}
	}

	assert(inst_clocks > 0);
	total_clocks += inst_clocks;

	buf_printf(asm_inst, "%s %s%s%s%s ; clocks: +%d = %d", 
		mnemonics[inst->op], size, dst, sep, src, inst_clocks, total_clocks);
		
	return asm_inst;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s [--dump-memory] <filepath>\n", argv[0]);
		exit(1);
	}

	char *file_path = NULL;
	bool dump_memory = false;

	// read command line args
	for (int i=1; i<argc; ++i) {
		char *arg = argv[i];
		if (arg[0] == '-') {
			if (0 == strcmp(arg, "--dump-memory")) 
				dump_memory = true;
		} else {
			file_path = arg;
		}
	}

	// TODO(shaw): read file directly into memory buffer to avoid the copy
	char *file_data;
	size_t file_size;
	bool ok = read_entire_file(file_path, &file_data, &file_size);
	if (!ok) {
		fprintf(stderr, "Failed to read file %s\n", argv[1]);
		exit(1);
	}
	assert(file_size <= 1*MB);
	memcpy(memory, file_data, file_size);

	Arena string_arena = {0};
	BUF(char *dasm_buf) = NULL;

	buf_printf(dasm_buf, "bits 16\n");

	// disassemble 
	while (regs[REG_IP] < file_size) {
		Instruction inst = decode_instruction();
		char *asm_inst = disassemble_instruction(&string_arena, &inst);
		buf_printf(dasm_buf, "%s\n", asm_inst);
	}

	FILE *fp = fopen("test.asm", "w");
	if (!fp) {
		perror("fopen");
		exit(1);
	}

	size_t count = buf_lenu(dasm_buf);
	if (fwrite(dasm_buf, 1, count, fp) < count) {
		perror("fwrite");
		fclose(fp);
		exit(1);
	}
	fclose(fp);

	// execute
	regs[REG_IP] = 0;
	while (regs[REG_IP] < file_size) {
		Instruction inst = decode_instruction();
		execute_instruction(&inst);
	}

	dump_registers();
	if (dump_memory) dump_memory_to_file();

	return 0;
}
