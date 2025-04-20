#ifndef _BIOS_EMU_INSTRUCTIONS_2_H
#define _BIOS_EMU_INSTRUCTIONS_2_H

typedef enum TwoBytesOpcodes {
	OP_LAR	   = 0x02,
	OP_LSL	   = 0x03,
	OP_SYSCALL = 0x05,
	OP_CLTS	   = 0x06,
	OP_SYSRET  = 0x07,
	OP_INVD	   = 0x08,
	OP_WBINVD  = 0x09,

	OP_BNDCL_BNDCU_BNDLDX_BNDMOV = 0x1A,
	OP_BNDCN_BNDMK_BNDSTX_BNDMOV = 0x1B,

	OP_NOP_rm = 0x1F,

	OP_MOV_r_cr = 0x20,
	OP_MOV_cr_r = 0x22,
	OP_MOV_r_dr = 0x21,
	OP_MOV_dr_r = 0x23,

	OP_WRMSR	= 0x30,
	OP_RDTSC	= 0x31,
	OP_RDMSR	= 0x32,
	OP_RDPMC	= 0x33,
	OP_SYSENTER = 0x34,
	OP_SYSEXIT	= 0x35,
	OP_GETSEC	= 0x36,

	OP_CMOVcc = 0x40,

	OP_MOVD_mm_rm32 = 0x6E,
	OP_MOVD_rm32_mm = 0x7E,

	OP_Jcc = 0x80,

	OP_SETcc = 0x90,

	OP_POP_FS = 0xA1,
	OP_POP_GS = 0xA9,

	OP_PUSH_FS = 0xA0,
	OP_PUSH_GS = 0xA8,

	OP_CPUID	 = 0xA2,
	OP_BT		 = 0xA3,
	OP_SHLD_imm8 = 0xA4,
	OP_SHLD_cl	 = 0xA5,
	OP_RSM		 = 0xAA,
	OP_BTS		 = 0xAB,
	OP_SHRD_imm8 = 0xAC,
	OP_SHRD_cl	 = 0xAD,

	OP_IMUL_r_rm = 0xAF,

	OP_CMPXCHG8 = 0xB0,
	OP_CMPXCHG	= 0xB1,

	OP_LSS = 0xB2,
	OP_BTR = 0xB3,
	OP_LFS = 0xB4,
	OP_LGS = 0xB5,

	OP_MOVZX_r_rm8	= 0xB6,
	OP_MOVZX_r_rm16 = 0xB7,
	OP_MOVSX_r_rm8	= 0xBE,
	OP_MOVSX_r_rm16 = 0xBF,

	OP_POPCNT = 0xB8,

	OP_BTC		 = 0xBB,
	OP_BSF_TZCNT = 0xBC,
	OP_BSR_LZCNT = 0xBD,

	OP_XADD_rm_r_8 = 0xC0,
	OP_XADD_rm_r   = 0xC1,
} TwoBytesOpcodes;

#endif