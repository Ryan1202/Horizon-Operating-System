#ifndef _BIOS_EMU_INSTRUCTIONS_1_H
#define _BIOS_EMU_INSTRUCTIONS_1_H

typedef enum OneByteOpcodes {
	// One Byte Opcode Instructions A-L
	OP_AAA = 0x37,
	OP_AAD = 0xD5,
	OP_AAM = 0xD4,
	OP_AAS = 0x3F,

	OP_ADC_imm8	  = 0x14,
	OP_ADC_imm	  = 0x15,
	OP_ADC_rm_r_8 = 0x10,
	OP_ADC_rm_r	  = 0x11,
	OP_ADC_r_rm_8 = 0x12,
	OP_ADC_r_rm	  = 0x13,

	OP_ADC_ADD_rm_imm_8 = 0x80,
	OP_ADC_ADD_rm_imm	= 0x81,
	OP_ADC_ADD_rm_imm8	= 0x83,

	OP_ADD_imm8	  = 0x04,
	OP_ADD_imm	  = 0x05,
	OP_ADD_rm_r_8 = 0x00,
	OP_ADD_rm_r	  = 0x01,
	OP_ADD_r_rm_8 = 0x02,
	OP_ADD_r_rm	  = 0x03,

	OP_AND_imm8	  = 0x24,
	OP_AND_imm	  = 0x25,
	OP_AND_rm_r_8 = 0x20,
	OP_AND_rm_r	  = 0x21,
	OP_AND_r_rm_8 = 0x22,
	OP_AND_r_rm	  = 0x23,

	OP_BOUND	= 0x62,
	OP_CALL		= 0xE8,
	OP_CALL_ptr = 0x9A,
	OP_CBW_CWDE = 0x98,
	OP_CLC		= 0xF8,
	OP_CLD		= 0xFC,
	OP_CLI		= 0xFA,
	OP_CMC		= 0xF5,

	OP_CMP_imm8	  = 0x3c,
	OP_CMP_imm	  = 0x3d,
	OP_CMP_rm_r_8 = 0x38,
	OP_CMP_rm_r	  = 0x39,
	OP_CMP_r_rm_8 = 0x3A,
	OP_CMP_r_rm	  = 0x3B,

	OP_CMPS8   = 0xA6,
	OP_CMPS	   = 0xA7,
	OP_CWD_CDQ = 0x99,
	OP_DAA	   = 0x27,
	OP_DAS	   = 0x2F,

	OP_DEC8	  = 0xFE,
	OP_DEC_rm = 0xFF,
	OP_DEC	  = 0x48,

	OP_DIV8 = 0xF6,
	OP_DIV	= 0xF6,

	OP_ENTER = 0xC8,
	OP_HLT	 = 0xF4,

	OP_IDIV8 = 0xF6,
	OP_IDIV	 = 0xF7,

	OP_IMUL8	  = 0xF6,
	OP_IMUL		  = 0xF7,
	OP_IMUL_imm8  = 0x6B,
	OP_IMUL_imm16 = 0x69,

	OP_IN8	  = 0xE4,
	OP_IN	  = 0xE5,
	OP_IN8_dx = 0xEC,
	OP_IN_dx  = 0xED,

	OP_INC8		= 0xFE,
	OP_INC_rm16 = 0xFF,
	OP_INC_rm32 = 0xFF,
	OP_INC		= 0x40,

	OP_INS8 = 0x6C,
	OP_INS	= 0x6D,

	OP_INT3 = 0xCC,
	OP_INT	= 0xCD,
	OP_INTO = 0xCE,
	OP_INT1 = 0xF1,

	OP_IRET_IRETD = 0xCF,

	OP_JCXZ_JECXZ_JRCXZ = 0xE3,

	OP_JE_JZ_8		 = 0x74,
	OP_JG_JNLE_8	 = 0x7F,
	OP_JGE_JNL_8	 = 0x7D,
	OP_JL_JNGE_8	 = 0x7C,
	OP_JLE_JNG_8	 = 0x7E,
	OP_JBE_JNA_8	 = 0x76,
	OP_JB_JC_JNAE_8	 = 0x72,
	OP_JAE_JNB_JNC_8 = 0x73,
	OP_JA_JNBE_8	 = 0x77,
	OP_JNE_JNZ_8	 = 0x75,
	OP_JNO_8		 = 0x71,
	OP_JNP_JPO_8	 = 0x7B,
	OP_JNS_8		 = 0x79,
	OP_JO_8			 = 0x70,
	OP_JP_JPE_8		 = 0x7A,
	OP_JS_8			 = 0x78,

	OP_JMP8	  = 0xEB,
	OP_JMP	  = 0xE9,
	OP_JMP_rm = 0xFF,

	OP_LongJMP	= 0xEA,
	OP_LongJMPm = 0xFF,

	OP_LAHF	 = 0x9F,
	OP_LDS	 = 0xC5,
	OP_LES	 = 0xC4,
	OP_LEA	 = 0x8D,
	OP_LEAVE = 0xC9,
	OP_LODS8 = 0xAC,
	OP_LODS	 = 0xAD,

	OP_LOOP	  = 0xE2,
	OP_LOOPE  = 0xE1,
	OP_LOOPNE = 0xE0,

	// One Byte Opcode Instructions M-U

	OP_MOV_rm_r8 = 0x88,
	OP_MOV_rm_r	 = 0x89,
	OP_MOV_r_rm8 = 0x8A,
	OP_MOV_r_rm	 = 0x8B,

	OP_MOV_rm_sreg = 0x8C,
	OP_MOV_sreg_rm = 0x8E,

	OP_MOV_a_moffs8 = 0xA0,
	OP_MOV_a_moffs	= 0xA1,
	OP_MOV_moffs8_a = 0xA2,
	OP_MOV_moffs_a	= 0xA3,

	OP_MOV_r_imm8  = 0xB0,
	OP_MOV_r_imm   = 0xB8,
	OP_MOV_rm_imm8 = 0xC6,
	OP_MOV_rm_imm  = 0xC7,

	OP_MOVS8 = 0xA4,
	OP_MOVS	 = 0xA5,

	OP_MUL8 = 0xF6,
	OP_MUL	= 0xF7,

	OP_NEG8 = 0xF6,
	OP_NEG	= 0xF7,
	OP_NOP	= 0x90,
	OP_NOT8 = 0xF6,
	OP_NOT	= 0xF7,

	OP_OR_rm_imm_8 = 0x80,
	OP_OR_rm_imm   = 0x81,
	OP_OR_rm_imm8  = 0x83,

	OP_OR_imm8	 = 0x0C,
	OP_OR_imm	 = 0x0D,
	OP_OR_rm_r_8 = 0x08,
	OP_OR_rm_r	 = 0x09,
	OP_OR_r_rm_8 = 0x0A,
	OP_OR_r_rm	 = 0x0B,

	OP_OUT8	   = 0xE6,
	OP_OUT	   = 0xE7,
	OP_OUT8_dx = 0xEE,
	OP_OUT_dx  = 0xEF,

	OP_OUTS8 = 0x6E,
	OP_OUTS	 = 0x6F,

	OP_POP_rm = 0x8F,
	OP_POP_r  = 0x58,
	OP_POP_DS = 0x1F,
	OP_POP_ES = 0x07,
	OP_POP_SS = 0x17,

	OP_POPA_POPAD = 0x61,
	OP_POPF_POPFD = 0x9D,

	OP_PUSH_rm	 = 0xFF,
	OP_PUSH_r	 = 0x50,
	OP_PUSH_imm8 = 0x6A,
	OP_PUSH_imm	 = 0x68,
	OP_PUSH_CS	 = 0x0E,
	OP_PUSH_SS	 = 0x16,
	OP_PUSH_DS	 = 0x1E,
	OP_PUSH_ES	 = 0x06,

	OP_PUSHA_PUSHAD = 0x60,
	OP_PUSHF_PUSHFD = 0x9C,

	OP_RET			 = 0xC3,
	OP_LongRET		 = 0xCB,
	OP_RET_imm16	 = 0xC2,
	OP_LongRET_imm16 = 0xCA,

	OP_SAHF = 0x9E,

	OP_SAL_SAR_SHL_SHR_8_1	  = 0xD0,
	OP_SAL_SAR_SHL_SHR_8_cl	  = 0xD2,
	OP_SAL_SAR_SHL_SHR_8_imm8 = 0xC0,

	OP_SAL_SAR_SHL_SHR_1	= 0xD1,
	OP_SAL_SAR_SHL_SHR_cl	= 0xD3,
	OP_SAL_SAR_SHL_SHR_imm8 = 0xC1,

	OP_SBB_rm_imm_8 = 0x80,
	OP_SBB_rm_imm	= 0x81,
	OP_SBB_rm_imm8	= 0x83,

	OP_SBB_imm8	  = 0x1C,
	OP_SBB_imm	  = 0x1D,
	OP_SBB_rm_r_8 = 0x18,
	OP_SBB_rm_r	  = 0x19,
	OP_SBB_r_rm_8 = 0x1A,
	OP_SBB_r_rm	  = 0x1B,

	OP_SCAS8 = 0xAE,
	OP_SCAS	 = 0xAF,
	OP_STC	 = 0xF9,
	OP_STD	 = 0xFD,
	OP_STI	 = 0xFB,
	OP_STOS8 = 0xAA,
	OP_STOS	 = 0xAB,

	OP_SUB_rm_imm_8 = 0x80,
	OP_SUB_rm_imm	= 0x81,
	OP_SUB_rm_imm8	= 0x83,

	OP_SUB_imm8	  = 0x2C,
	OP_SUB_imm	  = 0x2D,
	OP_SUB_rm_r_8 = 0x28,
	OP_SUB_rm_r	  = 0x29,
	OP_SUB_r_rm_8 = 0x2A,
	OP_SUB_r_rm	  = 0x2B,

	OP_TEST_imm8	= 0xA8,
	OP_TEST_imm		= 0xA9,
	OP_TEST_rm_imm8 = 0xF6,
	OP_TEST_rm_imm	= 0xF7,
	OP_TEST_rm_r_8	= 0x84,
	OP_TEST_rm_r	= 0x85,

	// One Byte Opcode Instructions W-Z

	OP_WAIT_FWAIT = 0x9B,

	OP_XCHG_r = 0x90,
	OP_XCHG_8 = 0x86,
	OP_XCHG	  = 0x87,

	OP_XLAT = 0xD7,

	OP_XOR_rm_imm_8 = 0x80,
	OP_XOR_rm_imm	= 0x81,
	OP_XOR_rm_imm8	= 0x83,

	OP_XOR_imm8	  = 0x34,
	OP_XOR_imm	  = 0x35,
	OP_XOR_rm_r_8 = 0x30,
	OP_XOR_rm_r	  = 0x31,
	OP_XOR_r_rm_8 = 0x32,
	OP_XOR_r_rm	  = 0x33,

	OP_TWO_BYTES = 0x0F,
} OneByteOpcodes;

#endif