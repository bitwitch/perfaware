typedef enum {
	OP_NONE,
	OP_MOV,
	OP_ADD,
	OP_SUB,
	OP_CMP,
} Operation;

typedef enum {
	FIELD_NONE,
	FIELD_OPCODE,
	FIELD_DIR,
	FIELD_SIGN_EXTEND,
	FIELD_WIDE,
	FIELD_MODE,
	FIELD_REG,
	FIELD_REG_MEM,
	FIELD_SEG_REG,
	FIELD_DISP,
	FIELD_DATA,
	FIELD_SRC_IS_IMM,
	FIELD_COUNT,
} FieldKind;

typedef struct {
    FieldKind kind;
    int num_bits;
    uint8_t value;
} Field;

typedef struct {
	Operation op;
    Field fields[12];
} InstructionEncoding;

// NOTE(shaw): in the decoder, FIELD_DISP depends on FIELD_MODE being set before it
// and FIELD_DATA depends on FIELD_WIDE being set before it (and in one edge case 
// FIELD_SIGN_EXTEND, but usually this is zero so it doesn't matter in the general case)

InstructionEncoding instruction_table[] = {

    // register/memory to/from register
	{ OP_MOV, {
		{ FIELD_OPCODE,   6, 0x22 },
		{ FIELD_DIR,      1 },
		{ FIELD_WIDE,     1 },
		{ FIELD_MODE,     2 },
		{ FIELD_REG,      3 },
		{ FIELD_REG_MEM,  3 },
		{ FIELD_DISP,    16 },
	 }},

    // immediate to register/memory 
    { OP_MOV, {
        { FIELD_OPCODE,   7, 0x63 },
        { FIELD_WIDE,     1 },
        { FIELD_MODE,     2 },
        { FIELD_OPCODE,   3, 0x0 },
        { FIELD_REG_MEM,  3 },
        { FIELD_DISP,    16 },
        { FIELD_DATA,    16 },
		{ FIELD_DIR,      0, 0x0 },
		{ FIELD_SRC_IS_IMM, 0, 0x1 },
    }},

    // immediate to register
	{ OP_MOV, {
		{ FIELD_OPCODE,   4, 0xB },
		{ FIELD_WIDE,     1 },
		{ FIELD_REG,      3 },
		{ FIELD_DATA,    16 },
		{ FIELD_DIR,      0, 0x1 },
		{ FIELD_MODE,     0, 0x3 },
		{ FIELD_SRC_IS_IMM, 0, 0x1 },
	}},

	// memory to accumulator
	{ OP_MOV, {
		{ FIELD_OPCODE,   4, 0x50 },
		{ FIELD_WIDE,     1 },
		{ FIELD_MODE,     0, 0x0 }, // this must precede FIELD_DISP
		{ FIELD_DISP,    16 },
		{ FIELD_REG,      0, 0x0 },
		{ FIELD_DIR,      0, 0x1 },
	}},

	// accumulator to memory
	{ OP_MOV, {
		{ FIELD_OPCODE,   4, 0x51 },
		{ FIELD_WIDE,     1 },
		{ FIELD_MODE,     0, 0x0 }, // this must precede FIELD_DISP
		{ FIELD_DISP,    16 },
		{ FIELD_REG,      0, 0x0 },
		{ FIELD_DIR,      0, 0x0 },
	}},

    // register/memory to segment register
	{ OP_MOV, {
		{ FIELD_OPCODE,   8, 0x8E },
		{ FIELD_MODE,     2 },
		{ FIELD_OPCODE,   1, 0x0 },
		{ FIELD_SEG_REG,  2 },
		{ FIELD_REG_MEM,  3 },
		{ FIELD_DISP,    16 },
		{ FIELD_DIR,      0, 0x1 },
	}},

    // segment registe to rregister/memory
	{ OP_MOV, {
		{ FIELD_OPCODE,   8, 0x8E },
		{ FIELD_MODE,     2 },
		{ FIELD_OPCODE,   1, 0x0 },
		{ FIELD_SEG_REG,  2 },
		{ FIELD_REG_MEM,  3 },
		{ FIELD_DISP,    16 },
		{ FIELD_DIR,      0, 0x0 },
	}},
};








