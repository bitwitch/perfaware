#define DISASSEM_BUF_SIZE       4096
#define SINGLE_INST_BUF_SIZE    64
#define OPERAND_BUF_SIZE        32
#define MB                      1024*1024

typedef enum {
	REG_A,
	REG_B,
	REG_C,
	REG_D,
	REG_SI,
	REG_DI,
	REG_SP,
	REG_BP,
	REG_IP,
	REG_COUNT,
} RegIndex;

typedef struct {
	RegIndex index;
	uint8_t size; // in bytes
	uint8_t offset;
} Register;

typedef enum {
	OPERAND_NONE,
	OPERAND_IMM,
	OPERAND_REL_IMM,
	OPERAND_REG,
	OPERAND_MEM,
} OperandKind;

typedef struct {
	Register reg_base;
	Register reg_offset;
	uint16_t imm_offset;
	bool is_direct;
	bool has_reg_offset;
} EffectiveAddress;

typedef struct {
	OperandKind kind;
	union {
		uint16_t imm;
		Register reg;
		EffectiveAddress addr;
	};
} Operand;

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

typedef struct {
	Operation op;
	Operand operands[2];
	bool wide;
} Instruction;

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