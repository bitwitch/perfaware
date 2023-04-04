// NOTE(shaw): this is currently used for conditional jump instructions, 
// not sure if this is a good idea yet, just needed some way to specify
// that conditional jumps will affect the ip reg
#define REG_IP 69

typedef enum {
	OP_NONE,
	OP_MOV,
	OP_ADD,
	OP_ADC,
	OP_INC,
	OP_AAA,
	OP_DAA,
	OP_SUB,
	OP_SBB,
	OP_DEC,
	OP_NEG,
	OP_CMP,
	OP_JZ,
	OP_JL,
	OP_JLE,
	OP_JB, 
	OP_JBE,
	OP_JP, 
	OP_JO, 
	OP_JS, 
	OP_JNE,
	OP_JGE,
	OP_JG, 
	OP_JNB,
	OP_JA, 
	OP_JNP,
	OP_JNO,
	OP_JNS,
	OP_LOOP,
	OP_LOOPZ,
	OP_LOOPNZ,
	OP_JCXZ,
	OP_COUNT,
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
	FIELD_SRC_IMM,
	FIELD_REL_JMP,
	FIELD_COUNT,
} FieldKind;

typedef struct {
    FieldKind kind;
    int num_bits;
    uint8_t value;
} Field;

typedef struct {
	Operation op;
    Field fields[16];
} InstructionEncoding;

#define CODE(num_bits, val) { FIELD_OPCODE, (num_bits), (val) }
#define D { FIELD_DIR, 1 }
#define S { FIELD_SIGN_EXTEND, 1 }
#define W { FIELD_WIDE, 1 }
#define MOD { FIELD_MODE, 2 }
#define REG { FIELD_REG, 3 }
#define RM { FIELD_REG_MEM, 3 }
#define SR { FIELD_SEG_REG, 2 }
#define DISP { FIELD_DISP, 16 }
#define DATA { FIELD_DATA, 16 }
#define SRC_IMM { FIELD_SRC_IMM, 0, 1 }
#define REL_JMP { FIELD_REL_JMP, 0, 1 }
#define SET_D(val) { FIELD_DIR, 0, (val) }
#define SET_S(val) { FIELD_SIGN_EXTEND, 0, (val) }
#define SET_MOD(val) { FIELD_MODE, 0, (val) }
#define SET_REG(val) { FIELD_REG, 0, (val) }
#define IP_INC8 SET_S(1), DATA, SET_D(1), SET_REG(REG_IP), SET_MOD(3), SRC_IMM, REL_JMP

// NOTE(shaw): in the decoder:
//     FIELD_DISP depends on FIELD_MODE being set before it
//     FIELD_DATA depends on FIELD_WIDE and FIELD_SIGN_EXTEND being set before it 

InstructionEncoding instruction_table[] = {

//------------------------------------------------------------------------------
//                     MOV
//------------------------------------------------------------------------------
    // register/memory to/from register
	{ OP_MOV, { CODE(6, 0x22), D, W, MOD, REG, RM, DISP }},

    // immediate to register/memory 
    { OP_MOV, { CODE(7, 0x63), W, MOD, CODE(3, 0), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate to register
	{ OP_MOV, { CODE(4, 0x0B), W, REG, DATA, SET_D(1), SET_MOD(0x3), SRC_IMM }},

	// memory to accumulator
	{ OP_MOV, { CODE(4, 0x50), W, SET_MOD(0), DISP, SET_REG(0), SET_D(1) }},

	// accumulator to memory
	{ OP_MOV, { CODE(4, 0x51), W, SET_MOD(0), DISP, SET_REG(0), SET_D(0) }},

    // register/memory to segment register
	{ OP_MOV, { CODE(8, 0x8E), MOD, CODE(1, 0), SR, RM, DISP, SET_D(1) }},

    // segment registe to register/memory
	{ OP_MOV, { CODE(8, 0x8E), MOD, CODE(1, 0), SR, RM, DISP, SET_D(0) }},

//------------------------------------------------------------------------------
//                     ADD
//------------------------------------------------------------------------------
    // reg/memory with register to either
    { OP_ADD, { CODE(6, 0), D, W, MOD, REG, RM, DISP }},

    // immediate to register/memory
    { OP_ADD, { CODE(6, 0x20), S, W, MOD, CODE(3, 0), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate to accumulator
	{ OP_ADD, { CODE(7, 0x02), W, DATA, SET_REG(0), SET_D(1), SRC_IMM }},

//------------------------------------------------------------------------------
//                     ADC
//------------------------------------------------------------------------------
    // reg/memory with register to either
    { OP_ADC, { CODE(6, 0x04), D, W, MOD, REG, RM, DISP }},

    // immediate to register/memory
    { OP_ADC, { CODE(6, 0x20), S, W, MOD, CODE(3, 0), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate to accumulator
	{ OP_ADC, { CODE(7, 0x0A), W, DATA, SET_REG(0), SET_D(1), SRC_IMM }},

//------------------------------------------------------------------------------
//                     INC
//------------------------------------------------------------------------------
     // register/memory 
    { OP_INC, { CODE(7, 0x7F), W, MOD, CODE(3, 0), RM, DISP, SET_D(0) }},

     // register
    { OP_INC, { CODE(5, 0x08), REG, SET_D(1) }},

//------------------------------------------------------------------------------
//                     AAA
//------------------------------------------------------------------------------
    // ASCII adjust for add
    { OP_AAA, { CODE(8, 0x37) }},
 
//------------------------------------------------------------------------------
//                     DAA
//------------------------------------------------------------------------------
    // Decimal adjust for add
    { OP_DAA, { CODE(8, 0x27) }},

//------------------------------------------------------------------------------
//                     SUB
//------------------------------------------------------------------------------
    // reg/memory and register to either
    { OP_SUB, { CODE(6, 0x0A), D, W, MOD, REG, RM, DISP }},

    // immediate from register/memory
    { OP_SUB, { CODE(6, 0x20), S, W, MOD, CODE(3, 0x5), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate from accumulator
	{ OP_SUB, { CODE(7, 0x16), W, DATA, SET_REG(0), SET_D(1), SRC_IMM }},

//------------------------------------------------------------------------------
//                     SBB
//------------------------------------------------------------------------------
    // reg/memory and register to either
    { OP_SBB, { CODE(6, 0x06), D, W, MOD, REG, RM, DISP }},

    // immediate from register/memory
    { OP_SBB, { CODE(6, 0x20), S, W, MOD, CODE(3, 0x3), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate from accumulator
	{ OP_SBB, { CODE(7, 0x0E), W, DATA, SET_REG(0), SET_D(1), SRC_IMM }},

//------------------------------------------------------------------------------
//                     DEC
//------------------------------------------------------------------------------
     // register/memory 
    { OP_DEC, { CODE(7, 0x7F), W, MOD, CODE(3, 0x1), RM, DISP, SET_D(0) }},

     // register
    { OP_DEC, { CODE(5, 0x09), REG, SET_D(1) }},

//------------------------------------------------------------------------------
//                     NEG
//------------------------------------------------------------------------------
    { OP_NEG, { CODE(7, 0x7B), W, MOD, CODE(3, 0x3), RM, DISP, SET_D(0) }},
 
//------------------------------------------------------------------------------
//                     CMP
//------------------------------------------------------------------------------
    // reg/memory and register
    { OP_CMP, { CODE(6, 0x0E), D, W, MOD, REG, RM, DISP }},

    // immediate with register/memory
    { OP_CMP, { CODE(6, 0x20), S, W, MOD, CODE(3, 0x7), RM, DISP, DATA, SET_D(0), SRC_IMM }},

    // immediate with accumulator
	{ OP_CMP, { CODE(7, 0x1E), W, DATA, SET_REG(0), SET_D(1), SRC_IMM }},


//------------------------------------------------------------------------------
//                     Conditional Jumps
//------------------------------------------------------------------------------
	{ OP_JZ,     { CODE(8, 0x74), IP_INC8 }},
	{ OP_JL,     { CODE(8, 0x7C), IP_INC8 }},
	{ OP_JLE,    { CODE(8, 0x7E), IP_INC8 }},
	{ OP_JB,     { CODE(8, 0x72), IP_INC8 }},
	{ OP_JBE,    { CODE(8, 0x76), IP_INC8 }},
	{ OP_JP,     { CODE(8, 0x7A), IP_INC8 }},
	{ OP_JO,     { CODE(8, 0x70), IP_INC8 }},
	{ OP_JS,     { CODE(8, 0x78), IP_INC8 }},
	{ OP_JNE,    { CODE(8, 0x75), IP_INC8 }},
	{ OP_JGE,    { CODE(8, 0x7D), IP_INC8 }},
	{ OP_JG,     { CODE(8, 0x7F), IP_INC8 }},
	{ OP_JNB,    { CODE(8, 0x73), IP_INC8 }},
	{ OP_JA,     { CODE(8, 0x77), IP_INC8 }},
	{ OP_JNP,    { CODE(8, 0x7B), IP_INC8 }},
	{ OP_JNO,    { CODE(8, 0x71), IP_INC8 }},
	{ OP_JNS,    { CODE(8, 0x79), IP_INC8 }},
	{ OP_LOOP,   { CODE(8, 0xE2), IP_INC8 }},
	{ OP_LOOPZ,  { CODE(8, 0xE1), IP_INC8 }},
	{ OP_LOOPNZ, { CODE(8, 0xE0), IP_INC8 }},
	{ OP_JCXZ,   { CODE(8, 0xE3), IP_INC8 }},

};

#undef CODE
#undef D
#undef S
#undef W
#undef MOD
#undef REG
#undef RM
#undef SR
#undef DISP
#undef DATA
#undef SRC_IMM
#undef REL_JMP
#undef SET_D
#undef SET_S
#undef SET_MOD
#undef SET_REG
#undef IP_INC8
