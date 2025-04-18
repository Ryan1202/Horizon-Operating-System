#ifndef _BIOS_EMU_DECODE_H
#define _BIOS_EMU_DECODE_H

#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#define PLUS_RB_REG(env, reg) (env)->reg_lut_r8[(reg) & 0x07]

#define PLUS_RW_REG(env, reg) (env)->reg_lut_r16[(reg) & 0x07]

#define PLUS_RD_REG(env, reg) (env)->reg_lut_r32[(reg) & 0x07]

typedef enum OperandNum {
	OPNUM_0 = 0,
	OPNUM_1,
	OPNUM_2,
	OPNUM_3,
} OperandNum;

typedef enum OperandDataType {
	OPDT_Byte,
	OPDT_Word,
	OPDT_Dword,
	OPDT_Qword,
} OperandDataType;

#define DEF_OP1(type)                        \
	typedef BiosEmuExceptions (*Op1_##type)( \
		BiosEmuEnvironment * env, uint##type##_t * value);

#define DEF_OP2(type1, type2)                               \
	typedef BiosEmuExceptions (*Op2_##type1##_##type2)(     \
		BiosEmuEnvironment * env, uint##type1##_t * value1, \
		uint##type2##_t * value2);

#define DEF_OP3_1(type, data_type_1, data_type2, data_type)              \
	typedef BiosEmuExceptions (*Op3_##type##data_type)(                  \
		BiosEmuEnvironment * env, data_type_1 value1, data_type2 value2, \
		uint##data_type##_t value3);

#define DEF_OP3_2(type, data_type_1, data_type)                          \
	DEF_OP3_1(type##data_type##_, data_type_1, uint##data_type##_t, 8);  \
	DEF_OP3_1(type##data_type##_, data_type_1, uint##data_type##_t, 16); \
	DEF_OP3_1(type##data_type##_, data_type_1, uint##data_type##_t, 32);

#define DEF_OP3_3(data_type)                            \
	DEF_OP3_2(data_type##_, uint##data_type##_t *, 8);  \
	DEF_OP3_2(data_type##_, uint##data_type##_t *, 16); \
	DEF_OP3_2(data_type##_, uint##data_type##_t *, 32);

typedef BiosEmuExceptions (*Op0)(BiosEmuEnvironment *env);
DEF_OP1(8);
DEF_OP1(16);
DEF_OP1(32);
DEF_OP2(8, 8);
DEF_OP2(8, 16);
DEF_OP2(8, 32);
DEF_OP2(16, 8);
DEF_OP2(16, 16);
DEF_OP2(16, 32);
DEF_OP2(32, 8);
DEF_OP2(32, 16);
DEF_OP2(32, 32);
DEF_OP3_3(8);
DEF_OP3_3(16);
DEF_OP3_3(32);

BiosEmuExceptions decode_rm8_r8(BiosEmuEnvironment *env, Op2_8_8 func);
BiosEmuExceptions decode_rm_r(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32);
BiosEmuExceptions decode_rm_imm8(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32);
BiosEmuExceptions decode_r_rm(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32);
BiosEmuExceptions decode_rm_r_imm8(
	BiosEmuEnvironment *env, Op3_16_16_8 func16, Op3_32_32_8 func32);
BiosEmuExceptions decode_rm_r_cl(
	BiosEmuEnvironment *env, Op3_16_16_8 func16, Op3_32_32_8 func32);

#endif