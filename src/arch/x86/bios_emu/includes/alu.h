#ifndef _BIOS_EMU_ALU_H
#define _BIOS_EMU_ALU_H

#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>

typedef enum {
	CALC_ADD = 0,
	CALC_ADC,
	CALC_AND,
	CALC_XOR,
	CALC_OR,
	CALC_SUB,
	CALC_SBB,
	CALC_CMP,
	CALC_TEST,
} CalcIndex;

typedef void (*Calc8)(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src);
typedef void (*Calc16)(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src);
typedef void (*Calc32)(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src);

void calc_a_imm8(BiosEmuEnvironment *env, CalcIndex index);
void calc_a_imm(BiosEmuEnvironment *env, CalcIndex index);
void calc_rm_imm_8(BiosEmuEnvironment *env, CalcIndex index);
void calc_rm_imm(BiosEmuEnvironment *env, CalcIndex index);
void calc_rm_imm8(BiosEmuEnvironment *env, CalcIndex index);
void calc_rm_r_8(BiosEmuEnvironment *env, CalcIndex index);
void calc_rm_r(BiosEmuEnvironment *env, CalcIndex index);
void calc_r_rm_8(BiosEmuEnvironment *env, CalcIndex index);
void calc_r_rm(BiosEmuEnvironment *env, CalcIndex index);

#define ALU_CASE(name)                 \
	case OP_##name##_imm8:             \
		calc_a_imm8(env, CALC_##name); \
		break;                         \
	case OP_##name##_imm:              \
		calc_a_imm(env, CALC_##name);  \
		break;                         \
	case OP_##name##_rm_r_8:           \
		calc_rm_r_8(env, CALC_##name); \
		break;                         \
	case OP_##name##_rm_r:             \
		calc_rm_r(env, CALC_##name);   \
		break;                         \
	case OP_##name##_r_rm_8:           \
		calc_r_rm_8(env, CALC_##name); \
		break;                         \
	case OP_##name##_r_rm:             \
		calc_r_rm(env, CALC_##name);   \
		break;

void decode_inc_r(BiosEmuEnvironment *env, uint8_t opcode);
void decode_dec_r(BiosEmuEnvironment *env, uint8_t opcode);
void decode_inc_rm(BiosEmuEnvironment *env);
void decode_dec_rm(BiosEmuEnvironment *env);
void decode_inc_rm8(BiosEmuEnvironment *env);
void decode_dec_rm8(BiosEmuEnvironment *env);

void decode_aaa(BiosEmuEnvironment *env);
void decode_aad(BiosEmuEnvironment *env);
void decode_aam(BiosEmuEnvironment *env);
void decode_aas(BiosEmuEnvironment *env);
void decode_daa(BiosEmuEnvironment *env);
void decode_das(BiosEmuEnvironment *env);

BiosEmuExceptions div_8(BiosEmuEnvironment *env, uint8_t value);
BiosEmuExceptions div_16(BiosEmuEnvironment *env, uint16_t value);
BiosEmuExceptions div_32(BiosEmuEnvironment *env, uint32_t value);
BiosEmuExceptions idiv_8(BiosEmuEnvironment *env, uint8_t value);
BiosEmuExceptions idiv_16(BiosEmuEnvironment *env, uint16_t value);
BiosEmuExceptions idiv_32(BiosEmuEnvironment *env, uint32_t value);

void mul_8(BiosEmuEnvironment *env, uint8_t value);
void mul_16(BiosEmuEnvironment *env, uint16_t value);
void mul_32(BiosEmuEnvironment *env, uint32_t value);
void imul_8(BiosEmuEnvironment *env, uint8_t value);
void imul_16(
	BiosEmuEnvironment *env, uint16_t *dst_hi, uint16_t *dst_lo, uint16_t src1,
	uint16_t src2);
void imul_32(
	BiosEmuEnvironment *env, uint32_t *dst_hi, uint32_t *dst_lo, uint32_t src1,
	uint32_t src2);

void decode_imul_r_rm_imm8(BiosEmuEnvironment *env);
void decode_imul_r_rm_imm16(BiosEmuEnvironment *env);
void decode_imul_r_rm(BiosEmuEnvironment *env);

void neg_8(BiosEmuEnvironment *env, uint8_t *value);
void neg_16(BiosEmuEnvironment *env, uint16_t *value);
void neg_32(BiosEmuEnvironment *env, uint32_t *value);

void rcl_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void rcl_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void rcl_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);
void rcr_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void rcr_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void rcr_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);
void rol_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void rol_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void rol_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);
void ror_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void ror_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void ror_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);

void shl_sal_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void shl_sal_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void shl_sal_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);
void shr_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void shr_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void shr_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);
void sar_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count);
void sar_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count);
void sar_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count);

#endif