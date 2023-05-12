#define DISASSEM_BUF_SIZE       4096
#define SINGLE_INST_BUF_SIZE    64
#define OPERAND_BUF_SIZE        32
#define MB                      1024*1024

// NOTE(shaw): order is important for most of these regs, as they are used as indices
typedef enum {
	REG_A  = 0,
	REG_B  = 1,
	REG_C  = 2,
	REG_D  = 3,
	REG_SP = 4,
	REG_BP = 5,
	REG_SI = 6,
	REG_DI = 7,
	REG_IP = 8,
	REG_ES,
	REG_CS,
	REG_SS,
	REG_DS,
	REG_FLAGS,
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
	OPERAND_SEG_REG,

	// only used for the instruction clocks table
	OPERAND_ACC,
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
	OP_JNZ,
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
	OP_JCXZ
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
	FIELD_HAS_SEG_REG,
	FIELD_CLOCKS,
	FIELD_COUNT,
} FieldKind;

typedef struct {
	FieldKind kind;
	int num_bits; // number of bits to read from the encoded instruction
	uint8_t value;
} Field;

typedef struct {
	Operation op;
	Field fields[16];
} InstructionEncoding;

typedef struct {
	OperandKind dst;
	OperandKind src;
	int clocks;
	bool add_ea_clocks;
} InstructionClocksEntry;

// these are masks for the corresponding bit in the flags register
typedef enum {
	FLAG_CARRY     = 0x0001,
	FLAG_PARITY    = 0x0004,
	FLAG_AUX_CARRY = 0x0008,
	FLAG_ZERO      = 0x0040,
	FLAG_SIGN      = 0x0080,
	FLAG_TRAP      = 0x0100,
	FLAG_INTERUPT  = 0x0200,
	FLAG_DIR       = 0x0400,
	FLAG_OVERFLOW  = 0x0800
} Flag;
