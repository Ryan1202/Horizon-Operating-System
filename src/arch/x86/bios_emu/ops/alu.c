#include "../includes/alu.h"
#include "../includes/decode.h"
#include "../includes/flags.h"
#include "../includes/mod_rm.h"
#include "bits.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

#define SET_FLAG(x, bit) \
	env->regs.flags =    \
		(x) ? env->regs.flags | BIT(bit) : env->regs.flags & ~BIT(bit)

#define SET_CARRY_FLAG(x)	 SET_FLAG(x, CarryFlagBit)
#define SET_OVERFLOW_FLAG(x) SET_FLAG(x, OverflowFlagBit)
#define SET_SIGN_FLAG(x)	 SET_FLAG(x, SignFlagBit)

#define SIGN_FLAG8(x)  SET_SIGN_FLAG(x >> 7)
#define SIGN_FLAG16(x) SET_SIGN_FLAG(x >> 15)
#define SIGN_FLAG32(x) SET_SIGN_FLAG(x >> 31)

static inline uint8_t calc_parity(uint8_t x) {
	x ^= x >> 4;
	x &= 0x0F;
	return (0x6996 >> x) & 1;
}

void set_test_flag(BiosEmuEnvironment *env, uint32_t x) {
	env->regs.flags = x == 0 ? env->regs.flags | BIT(ZeroFlagBit)
							 : env->regs.flags & ~BIT(ZeroFlagBit);
	env->regs.flags =
		(calc_parity(x) ? env->regs.flags | BIT(ParityFlagBit)
						: env->regs.flags & ~BIT(ParityFlagBit));
	env->regs.flags &= ~BIT(CarryFlagBit);
	env->regs.flags &= ~BIT(OverflowFlagBit);
	return;
}

static void test_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t temp = *dst & src;

	SIGN_FLAG8(temp);
	set_test_flag(env, temp);
	return;
}

static void test_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t temp = *dst & src;

	SIGN_FLAG16(temp);
	set_test_flag(env, temp);
	return;
}

static void test_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t temp = *dst & src;

	SIGN_FLAG32(temp);
	set_test_flag(env, temp);
	return;
}

void set_zf_pf(BiosEmuEnvironment *env, uint32_t x) {
	env->regs.flags = x == 0 ? env->regs.flags | BIT(ZeroFlagBit)
							 : env->regs.flags & ~BIT(ZeroFlagBit);
	env->regs.flags =
		(calc_parity(x) ? env->regs.flags | BIT(ParityFlagBit)
						: env->regs.flags & ~BIT(ParityFlagBit));
	return;
}

static void add_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t ans;
	int		overflow = __builtin_add_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst > ~src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG8(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void add_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans;
	int		 overflow = __builtin_add_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst > ~src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG16(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void add_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans;
	int		 overflow = __builtin_add_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst > ~src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG32(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void adc_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	add_8(env, dst, src + carry);
	return;
}

static void adc_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	add_16(env, dst, src + carry);
	return;
}

static void adc_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	add_32(env, dst, src + carry);
	return;
}

static void sub_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t ans;
	int		overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG8(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void sub_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans;
	int		 overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG16(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void sub_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans;
	int		 overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG32(ans);
	set_zf_pf(env, ans);
	*dst = ans;
	return;
}

static void sbb_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	sub_8(env, dst, src + carry);
	return;
}

static void sbb_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	sub_16(env, dst, src + carry);
	return;
}

static void sbb_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint8_t carry = !!(env->regs.flags & BIT(CarryFlagBit));
	sub_32(env, dst, src + carry);
	return;
}

static void and_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t ans = *dst & src;

	SIGN_FLAG8(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void and_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans = *dst & src;

	SIGN_FLAG16(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void and_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans = *dst & src;

	SIGN_FLAG32(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void xor_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t ans = *dst ^ src;

	SIGN_FLAG8(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void xor_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans = *dst ^ src;

	SIGN_FLAG16(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void xor_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans = *dst ^ src;

	SIGN_FLAG32(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void or_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint8_t ans = *dst | src;

	SIGN_FLAG8(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void or_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans = *dst | src;

	SIGN_FLAG16(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void or_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans = *dst | src;

	SIGN_FLAG32(ans);
	set_test_flag(env, ans);
	*dst = ans;
	return;
}

static void cmp_8(BiosEmuEnvironment *env, uint8_t *dst, uint8_t src) {
	uint16_t ans;
	int		 overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG16(ans);
	set_zf_pf(env, ans);
	return;
}

static void cmp_16(BiosEmuEnvironment *env, uint16_t *dst, uint16_t src) {
	uint16_t ans;
	int		 overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG16(ans);
	set_zf_pf(env, ans);
	return;
}

static void cmp_32(BiosEmuEnvironment *env, uint32_t *dst, uint32_t src) {
	uint32_t ans;
	int		 overflow = __builtin_sub_overflow(*dst, src, &ans);

	SET_CARRY_FLAG(*dst < src);
	SET_OVERFLOW_FLAG(overflow);
	SIGN_FLAG32(ans);
	set_zf_pf(env, ans);
	return;
}

Calc8 calc8[] = {
	[CALC_ADD] = add_8, [CALC_ADC] = adc_8, [CALC_AND] = and_8,
	[CALC_XOR] = xor_8, [CALC_OR] = or_8,	[CALC_SBB] = sbb_8,
	[CALC_SUB] = sub_8, [CALC_CMP] = cmp_8, [CALC_TEST] = test_8,
};

Calc16 calc16[] = {
	[CALC_ADD] = add_16, [CALC_ADC] = adc_16, [CALC_AND] = and_16,
	[CALC_XOR] = xor_16, [CALC_OR] = or_16,	  [CALC_SBB] = sbb_16,
	[CALC_SUB] = sub_16, [CALC_CMP] = cmp_16, [CALC_TEST] = test_16,
};

Calc32 calc32[] = {
	[CALC_ADD] = add_32, [CALC_ADC] = adc_32, [CALC_AND] = and_32,
	[CALC_XOR] = xor_32, [CALC_OR] = or_32,	  [CALC_SBB] = sbb_32,
	[CALC_SUB] = sub_32, [CALC_CMP] = cmp_32, [CALC_TEST] = test_32,
};

void calc_a_imm8(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t imm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	calc8[index](env, &env->regs.al, imm);
	return;
}

void calc_a_imm(BiosEmuEnvironment *env, CalcIndex index) {
	if (env->flags.operand_size == 0) {
		uint16_t imm = *(uint16_t *)env->cur_ip;
		env->regs.eip += 2;
		env->cur_ip += 2;
		calc16[index](env, &env->regs.ax, imm);
	} else {
		uint32_t imm = *(uint32_t *)env->cur_ip;
		env->regs.eip += 4;
		env->cur_ip += 4;
		calc32[index](env, &env->regs.eax, imm);
	}
	return;
}

void calc_rm_imm_8(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	uint8_t *dst = RM_ADDR(env, modrm);
	uint8_t	 imm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	calc8[index](env, dst, imm);
	return;
}

void calc_rm_imm(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = RM_ADDR(env, modrm);
		uint16_t  imm = *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;

		calc16[index](env, dst, imm);
	} else {
		uint32_t *dst = RM_ADDR(env, modrm);
		uint32_t  imm = *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;

		calc32[index](env, dst, imm);
	}
	return;
}

void calc_rm_imm8(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = RM_ADDR(env, modrm);
		uint8_t	  imm = *(uint8_t *)env->cur_ip;
		env->cur_ip++;
		env->regs.eip++;

		calc16[index](env, dst, (uint16_t)imm);
	} else {
		uint32_t *dst = RM_ADDR(env, modrm);
		uint8_t	  imm = *(uint8_t *)env->cur_ip;
		env->cur_ip++;
		env->regs.eip++;

		calc32[index](env, dst, (uint32_t)imm);
	}
	return;
}

void calc_rm_r_8(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *dst = RM_ADDR(env, modrm);
	uint8_t *src = env->reg_lut_r8[reg];

	calc8[index](env, dst, *src);
	return;
}

void calc_rm_r(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = RM_ADDR(env, modrm);
		uint16_t *src = env->reg_lut_r16[reg];

		calc16[index](env, dst, *src);
	} else {
		uint32_t *dst = RM_ADDR(env, modrm);
		uint32_t *src = env->reg_lut_r32[reg];

		calc32[index](env, dst, *src);
	}
	return;
}
void calc_r_rm_8(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *dst = env->reg_lut_r8[reg];
	uint8_t *src = RM_ADDR(env, modrm);

	calc8[index](env, dst, *src);
	return;
}

void calc_r_rm(BiosEmuEnvironment *env, CalcIndex index) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = env->reg_lut_r16[reg];
		uint16_t *src = RM_ADDR(env, modrm);

		calc16[index](env, dst, *src);
	} else {
		uint32_t *dst = env->reg_lut_r32[reg];
		uint32_t *src = RM_ADDR(env, modrm);

		calc32[index](env, dst, *src);
	}
	return;
}

void decode_inc_r(BiosEmuEnvironment *env, uint8_t opcode) {
	if (env->flags.operand_size == 0) {
		uint16_t *dst	   = PLUS_RW_REG(env, opcode);
		int		  overflow = __builtin_add_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	} else {
		uint32_t *dst = PLUS_RD_REG(env, opcode);
		__builtin_add_overflow(*dst, 1, dst);
		int overflow = __builtin_add_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	}
	return;
}

void decode_dec_r(BiosEmuEnvironment *env, uint8_t opcode) {
	if (env->flags.operand_size == 0) {
		uint16_t *dst	   = PLUS_RW_REG(env, opcode);
		int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	} else {
		uint32_t *dst	   = PLUS_RD_REG(env, opcode);
		int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	}
	return;
}

void decode_inc_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t mod = modrm >> 6;

	if (mod == 0b11) {
		uint8_t reg = modrm & 0b111;
		if (env->flags.operand_size == 0) {
			uint16_t *dst	   = env->reg_lut_r16[reg];
			int		  overflow = __builtin_add_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		} else {
			uint32_t *dst	   = env->reg_lut_r32[reg];
			int		  overflow = __builtin_add_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		}
		return;
	} else {
		if (env->flags.operand_size == 0) {
			uint16_t *dst	   = RM_ADDR16(env, modrm);
			int		  overflow = __builtin_add_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		} else {
			uint32_t *dst	   = RM_ADDR32(env, modrm);
			int		  overflow = __builtin_add_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		}
	}
	return;
}

void decode_dec_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t mod = modrm >> 6;

	if (mod == 0b11) {
		uint8_t reg = modrm & 0b111;
		if (env->flags.operand_size == 0) {
			uint16_t *dst	   = env->reg_lut_r16[reg];
			int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		} else {
			uint32_t *dst	   = env->reg_lut_r32[reg];
			int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		}
		return;
	} else {
		if (env->flags.operand_size == 0) {
			uint16_t *dst	   = RM_ADDR16(env, modrm);
			int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		} else {
			uint32_t *dst	   = RM_ADDR32(env, modrm);
			int		  overflow = __builtin_sub_overflow(*dst, 1, dst);
			SET_OVERFLOW_FLAG(overflow);
			set_zf_pf(env, *dst);
		}
	}
	return;
}

void decode_inc_rm8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t mod = modrm >> 6;

	if (mod == 0b11) {
		uint8_t	 reg	  = modrm & 0b111;
		uint8_t *dst	  = env->reg_lut_r8[reg];
		int		 overflow = __builtin_add_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	} else {
		uint8_t *dst	  = RM_ADDR(env, modrm);
		int		 overflow = __builtin_add_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	}
	return;
}

void decode_dec_rm8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t mod = modrm >> 6;

	if (mod == 0b11) {
		uint8_t	 reg	  = modrm & 0b111;
		uint8_t *dst	  = env->reg_lut_r8[reg];
		int		 overflow = __builtin_sub_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	} else {
		uint8_t *dst	  = RM_ADDR(env, modrm);
		int		 overflow = __builtin_sub_overflow(*dst, 1, dst);
		SET_OVERFLOW_FLAG(overflow);
		set_zf_pf(env, *dst);
	}
	return;
}

void decode_aaa(BiosEmuEnvironment *env) {
	if (env->regs.al > 0x9f || env->regs.flags & BIT(AuxiliaryCarryFlagBit)) {
		env->regs.ax += 0x106;
		SET_FLAG(1, AuxiliaryCarryFlagBit);
	} else {
		SET_FLAG(0, AuxiliaryCarryFlagBit);
	}
	return;
}

void decode_aad(BiosEmuEnvironment *env) {
	uint8_t base = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	env->regs.al = (env->regs.al + (env->regs.ah * base)) & 0xff;
	env->regs.ah = 0;

	SIGN_FLAG8(env->regs.al);
	SET_FLAG(env->regs.al == 0, ZeroFlagBit);
	SET_FLAG(calc_parity(env->regs.al), ParityFlagBit);
	return;
}

void decode_aam(BiosEmuEnvironment *env) {
	uint8_t base = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	env->regs.ah = env->regs.al / base;
	env->regs.al = env->regs.al % base;

	SIGN_FLAG8(env->regs.al);
	SET_FLAG(env->regs.al == 0, ZeroFlagBit);
	SET_FLAG(calc_parity(env->regs.al), ParityFlagBit);
	return;
}

void decode_aas(BiosEmuEnvironment *env) {
	if (env->regs.al > 0x0f || env->regs.flags & BIT(AuxiliaryCarryFlagBit)) {
		env->regs.al -= 6;
		env->regs.ah -= 1;
		env->regs.al &= 0x0f;
		env->regs.flags |= BIT(AuxiliaryCarryFlagBit) | BIT(CarryFlagBit);
	} else {
		env->regs.flags &= ~(BIT(AuxiliaryCarryFlagBit) | BIT(CarryFlagBit));
		env->regs.al &= 0x0f;
	}
	return;
}

void decode_daa(BiosEmuEnvironment *env) {
	uint8_t old_al = env->regs.al;
	uint8_t old_cf = env->regs.flags & BIT(CarryFlagBit);
	SET_CARRY_FLAG(0);
	if ((env->regs.al & 0x0f) > 9 ||
		env->regs.flags & BIT(AuxiliaryCarryFlagBit)) {
		env->regs.al += 0x06;

		env->regs.flags |= old_cf | BIT(AuxiliaryCarryFlagBit);
		/* (uint8_t)~6 == 255 - 6*/
		/* old_al > 255 - 6 => old_al + 6 > 255 */
		(old_al > (uint8_t)~6) ? (env->regs.flags |= BIT(CarryFlagBit))
							   : (env->regs.flags &= ~BIT(CarryFlagBit));
	} else {
		SET_FLAG(0, AuxiliaryCarryFlagBit);
	}
	if (env->regs.al > 0x99 || old_cf) {
		env->regs.al += 0x60;
		SET_CARRY_FLAG(1);
	} else {
		SET_CARRY_FLAG(0);
	}
	return;
}

void decode_das(BiosEmuEnvironment *env) {
	uint8_t old_al = env->regs.al;
	uint8_t old_cf = env->regs.flags & BIT(CarryFlagBit);
	SET_CARRY_FLAG(0);
	if ((env->regs.al & 0x0f) > 9 ||
		env->regs.flags & BIT(AuxiliaryCarryFlagBit)) {
		env->regs.al -= 0x06;

		env->regs.flags |= old_cf | BIT(AuxiliaryCarryFlagBit);
		(old_al < (uint8_t)-6) ? (env->regs.flags |= BIT(CarryFlagBit))
							   : (env->regs.flags &= ~BIT(CarryFlagBit));
	} else {
		SET_FLAG(0, AuxiliaryCarryFlagBit);
	}
	if (env->regs.al > 0x99 || old_cf) {
		env->regs.al -= 0x60;
		SET_CARRY_FLAG(1);
	}
}

BiosEmuExceptions div_8(BiosEmuEnvironment *env, uint8_t value) {
	if (value == 0) { return DivideError; }

	uint16_t dividend = env->regs.ax;
	uint8_t	 divisor  = env->regs.al;

	uint16_t temp1 = dividend / divisor;
	uint16_t temp2 = dividend % divisor;

	if (temp1 > 0xff) {
		return DivideError;
	} else {
		env->regs.al = temp1;
		env->regs.ah = temp2;
	}
	return NoException;
}

BiosEmuExceptions div_16(BiosEmuEnvironment *env, uint16_t value) {
	if (value == 0) { return DivideError; }

	uint32_t dividend = env->regs.dx << 16 | env->regs.ax;

	uint32_t temp1 = dividend / value;
	uint16_t temp2 = dividend % value;
	if (temp1 > 0xffff) {
		return DivideError;
	} else {
		env->regs.ax = temp1;
		env->regs.dx = temp2;
	}

	return NoException;
}

void div_64_32(
	uint64_t a, uint32_t b, uint64_t *quotient, uint32_t *remainder) {
	int		 ans = 0;
	uint64_t tmp = 0;
	int		 max = 64;
	if (b > a) {
		*quotient  = 0;
		*remainder = a;
		return;
	}
	for (int i = 0; i < 64; i++) {
		int j = 0;
		do {
			tmp = b << j;
			j++;
		} while (tmp < a && j < max);
		j--;
		if (j == 0) break;
		ans |= 1 << (j - 1);
		a -= b << (j - 1);
	}
	*quotient  = ans;
	*remainder = a;
}

BiosEmuExceptions div_32(BiosEmuEnvironment *env, uint32_t value) {
	if (value == 0) { return DivideError; }

	uint64_t dividend = ((uint64_t)env->regs.edx << 32) | env->regs.eax;

	uint64_t temp1;
	uint32_t temp2;
	div_64_32(dividend, value, &temp1, &temp2);
	if (temp1 > 0xffffffff) {
		return DivideError;
	} else {
		env->regs.eax = temp1;
		env->regs.edx = temp2;
	}

	return NoException;
}

BiosEmuExceptions idiv_8(BiosEmuEnvironment *env, uint8_t value) {
	if (value == 0) { return DivideError; }

	uint16_t dividend = env->regs.ax;
	uint8_t	 divisor  = env->regs.al;

	uint16_t temp1 = dividend / divisor;
	uint16_t temp2 = dividend % divisor;

	if (temp1 >> 7 && temp1 >> 7 != -1) {
		return DivideError;
	} else {
		env->regs.al = temp1;
		env->regs.ah = temp2;
	}
	return NoException;
}

BiosEmuExceptions idiv_16(BiosEmuEnvironment *env, uint16_t value) {
	if (value == 0) { return DivideError; }

	uint32_t dividend = env->regs.dx << 16 | env->regs.ax;

	uint32_t temp1 = dividend / value;
	uint16_t temp2 = dividend % value;
	if (temp1 >> 15 && temp1 >> 15 != -1) {
		return DivideError;
	} else {
		env->regs.ax = temp1;
		env->regs.dx = temp2;
	}

	return NoException;
}

BiosEmuExceptions idiv_32(BiosEmuEnvironment *env, uint32_t value) {
	if (value == 0) { return DivideError; }

	uint64_t dividend = ((uint64_t)env->regs.edx << 32) | env->regs.eax;

	uint64_t temp1;
	uint32_t temp2;
	div_64_32(dividend, value, &temp1, &temp2);
	if (temp1 >> 31 && temp1 >> 31 != -1) {
		return DivideError;
	} else {
		env->regs.eax = temp1;
		env->regs.edx = temp2;
	}

	return NoException;
}

void mul_8(BiosEmuEnvironment *env, uint8_t value) {
	uint16_t ans = env->regs.al * value;
	env->regs.ax = ans;
	SET_OVERFLOW_FLAG(ans >> 8);
}

void mul_16(BiosEmuEnvironment *env, uint16_t value) {
	uint32_t ans = env->regs.ax * value;
	env->regs.ax = ans & 0xffff;
	env->regs.dx = ans >> 16;
	SET_OVERFLOW_FLAG(ans >> 16);
}

void mul_32(BiosEmuEnvironment *env, uint32_t value) {
	uint64_t ans = env->regs.eax * value;
	env->regs.ax = ans & 0xffffffff;
	env->regs.dx = ans >> 32;
	SET_OVERFLOW_FLAG(ans >> 32);
}

void imul_8(BiosEmuEnvironment *env, uint8_t value) {
	int16_t ans	 = (int16_t)(int8_t)env->regs.al * (int16_t)(int8_t)value;
	env->regs.ax = ans;
	SET_OVERFLOW_FLAG(ans >> 7 != 0 && ans >> 7 != -1);
}

void imul_16(
	BiosEmuEnvironment *env, uint16_t *dst_hi, uint16_t *dst_lo, uint16_t src1,
	uint16_t src2) {
	int32_t ans = (int32_t)(int16_t)src1 * (int32_t)(int16_t)src2;
	*dst_hi		= ans >> 16;
	*dst_lo		= ans & 0xffff;
	SET_OVERFLOW_FLAG(ans >> 15 != 0 && ans >> 15 != -1);
}

void imul_32(
	BiosEmuEnvironment *env, uint32_t *dst_hi, uint32_t *dst_lo, uint32_t src1,
	uint32_t src2) {
	int64_t ans = (int64_t)(int32_t)src1 * (int64_t)(int32_t)src2;
	*dst_hi		= ans >> 32;
	*dst_lo		= ans & 0xffffffff;
	SET_OVERFLOW_FLAG(ans >> 31 != 0 && ans >> 31 != -1);
}

void decode_imul_r_rm_imm8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;
	uint8_t imm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;

	if (env->flags.operand_size == 0) {
		uint16_t  tmp;
		uint16_t *dst	= env->reg_lut_r16[reg];
		uint16_t *value = RM_ADDR(env, modrm);
		imul_16(env, &tmp, dst, *value, imm);
	} else {
		uint32_t  tmp;
		uint32_t *dst	= env->reg_lut_r32[reg];
		uint32_t *value = RM_ADDR(env, modrm);
		imul_32(env, &tmp, dst, *value, imm);
	}
	return;
}

void decode_imul_r_rm_imm16(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t  tmp;
		uint16_t *dst	= env->reg_lut_r16[reg];
		uint16_t *value = RM_ADDR(env, modrm);
		uint16_t  imm	= *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		imul_16(env, &tmp, dst, *value, imm);
	} else {
		uint32_t  tmp;
		uint32_t *dst	= env->reg_lut_r32[reg];
		uint32_t *value = RM_ADDR(env, modrm);
		uint32_t  imm	= *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		imul_32(env, &tmp, dst, *value, imm);
	}
	return;
}

void decode_imul_r_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t  tmp;
		uint16_t *dst	= env->reg_lut_r16[reg];
		uint16_t *value = RM_ADDR(env, modrm);
		imul_16(env, &tmp, dst, *dst, *value);
	} else {
		uint32_t  tmp;
		uint32_t *dst	= env->reg_lut_r32[reg];
		uint32_t *value = RM_ADDR(env, modrm);
		imul_32(env, &tmp, dst, *dst, *value);
	}
	return;
}

void neg_8(BiosEmuEnvironment *env, uint8_t *value) {
	int8_t ans = 0 - *value;
	SET_CARRY_FLAG(ans == 0);
	*value = ans;
	return;
}

void neg_16(BiosEmuEnvironment *env, uint16_t *value) {
	int16_t ans = 0 - (int16_t)*value;
	SET_CARRY_FLAG(ans == 0);
	*value = ans;
	return;
}

void neg_32(BiosEmuEnvironment *env, uint32_t *value) {
	int32_t ans = 0 - (int32_t)*value;
	SET_CARRY_FLAG(ans == 0);
	*value = ans;
	return;
}

void rcl_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t new_carry = (*value >> (8 - temp_count)) & 1;
	uint8_t ans = (*value << temp_count) | (old_carry << (temp_count - 1));

	ans |= (*value >> (8 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 7) ^ new_carry);
	*value = ans;
	return;
}

void rcl_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint8_t	 old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t	 new_carry = (*value >> (16 - temp_count)) & 1;
	uint16_t ans =
		((*value << temp_count) & 0xff) | (old_carry << (temp_count - 1));

	ans |= (*value >> (16 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 15) ^ new_carry);
	*value = ans;
	return;
}

void rcl_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 33;
	if (temp_count == 0) return;

	uint8_t	 old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t	 new_carry = (*value >> (32 - temp_count)) & 1;
	uint32_t ans =
		((*value << temp_count) & 0xff) | (old_carry << (temp_count - 1));

	ans |= (*value >> (32 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 31) ^ new_carry);
	*value = ans;
	return;
}

void rcr_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t new_carry = (*value >> (temp_count - 1)) & 1;
	uint8_t ans = (*value >> temp_count) | (old_carry << (8 - temp_count));

	ans |= (*value << (8 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((*value & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 7) ^ old_carry);
	*value = ans;
	return;
}

void rcr_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint8_t	 old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t	 new_carry = (*value >> (temp_count - 1)) & 1;
	uint16_t ans = (*value >> temp_count) | (old_carry << (16 - temp_count));

	ans |= (*value << (16 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((*value & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 15) ^ old_carry);
	*value = ans;
	return;
}

void rcr_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 33;
	if (temp_count == 0) return;

	uint8_t	 old_carry = (env->regs.flags >> CarryFlagBit) & 1;
	uint8_t	 new_carry = (*value >> (temp_count - 1)) & 1;
	uint32_t ans = (*value >> temp_count) | (old_carry << (32 - temp_count));

	ans |= (*value << (32 - temp_count + 1));

	SET_CARRY_FLAG(new_carry);
	if ((*value & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 31) ^ old_carry);
	*value = ans;
	return;
}

void rol_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t ans = (*value << temp_count) | (*value >> (8 - temp_count));

	if (ans & 0x1f) {
		SET_CARRY_FLAG(ans & 1);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 7) ^ (ans & 1));
	}
	*value = ans;
	return;
}

void rol_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint16_t ans = (*value << temp_count) | (*value >> (16 - temp_count));

	if (ans & 0x1f) {
		SET_CARRY_FLAG(ans & 1);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 15) ^ (ans & 1));
	}
	*value = ans;
	return;
}

void rol_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint32_t ans = (*value << temp_count) | (*value >> (32 - temp_count));

	if (ans & 0x1f) {
		SET_CARRY_FLAG(ans & 1);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 31) ^ (ans & 1));
	}
	*value = ans;
	return;
}

void ror_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t ans = (*value >> temp_count) | (*value << (8 - temp_count));

	uint8_t msb = ans >> 7;
	if (ans & 0x1f) {
		SET_CARRY_FLAG(msb);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG(msb ^ (ans >> 6));
	}
	*value = ans;
	return;
}

void ror_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint16_t ans = (*value >> temp_count) | (*value << (16 - temp_count));

	uint8_t msb = ans >> 15;
	if (ans & 0x1f) {
		SET_CARRY_FLAG(msb);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG(msb ^ (ans >> 14));
	}
	*value = ans;
	return;
}

void ror_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint32_t ans = (*value >> temp_count) | (*value << (32 - temp_count));

	uint8_t msb = ans >> 31;
	if (ans & 0x1f) {
		SET_CARRY_FLAG(msb);
		if ((ans & 0x1f) == 1) SET_OVERFLOW_FLAG(msb ^ (ans >> 30));
	}
	*value = ans;
	return;
}

void shl_sal_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t ans = (*value << temp_count);

	uint8_t carry = (*value >> (8 - temp_count)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 7) ^ carry);
	*value = ans;
	return;
}

void shl_sal_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint16_t ans = (*value << temp_count);

	uint8_t carry = (*value >> (16 - temp_count)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 15) ^ carry);
	*value = ans;
	return;
}

void shl_sal_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 33;
	if (temp_count == 0) return;

	uint16_t ans = (*value << temp_count);

	uint8_t carry = (*value >> (32 - temp_count)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG((ans >> 31) ^ carry);
	*value = ans;
	return;
}

void shr_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t ans = (*value >> temp_count);

	uint8_t carry = (*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(*value >> 7);
	*value = ans;
	return;
}

void shr_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint16_t ans = (*value >> temp_count);

	uint8_t carry = (*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(*value >> 15);
	*value = ans;
	return;
}

void shr_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 33;
	if (temp_count == 0) return;

	uint32_t ans = (*value >> temp_count);

	uint8_t carry = (*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(*value >> 31);
	*value = ans;
	return;
}

void sar_8(BiosEmuEnvironment *env, uint8_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 9;
	if (temp_count == 0) return;

	uint8_t ans = ((int8_t)*value >> temp_count);

	uint8_t carry = ((int8_t)*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(0);
	*value = ans;
	return;
}

void sar_16(BiosEmuEnvironment *env, uint16_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 17;
	if (temp_count == 0) return;

	uint16_t ans = ((int16_t)*value >> temp_count);

	uint8_t carry = ((int16_t)*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(0);
	*value = ans;
	return;
}

void sar_32(BiosEmuEnvironment *env, uint32_t *value, uint8_t count) {
	uint8_t temp_count = (count & 0x1f) % 33;
	if (temp_count == 0) return;

	uint32_t ans = ((int32_t)*value >> temp_count);

	uint8_t carry = ((int32_t)*value >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	if ((count & 0x1f) == 1) SET_OVERFLOW_FLAG(0);
	*value = ans;
	return;
}

BiosEmuExceptions shld_16_16_8(
	BiosEmuEnvironment *env, uint16_t *dest, uint16_t reg, uint8_t count) {
	uint8_t temp_count = count % 32;
	if (temp_count == 0 || count > 16) return NoException;

	uint16_t ans = (*dest << temp_count) | (reg >> (16 - temp_count));

	uint8_t carry = (reg >> (16 - temp_count)) & 1;
	SET_CARRY_FLAG(carry);
	set_zf_pf(env, ans);
	*dest = ans;
	return NoException;
}

BiosEmuExceptions shld_32_32_8(
	BiosEmuEnvironment *env, uint32_t *dest, uint32_t reg, uint8_t count) {
	uint8_t temp_count = count % 32;
	if (temp_count == 0 || count > 32) return NoException;

	uint32_t ans = (*dest << temp_count) | (reg >> (32 - temp_count));

	uint8_t carry = (reg >> (32 - temp_count)) & 1;
	SET_CARRY_FLAG(carry);
	set_zf_pf(env, ans);
	*dest = ans;
	return NoException;
}

BiosEmuExceptions shrd_16_16_8(
	BiosEmuEnvironment *env, uint16_t *dest, uint16_t reg, uint8_t count) {
	uint8_t temp_count = count % 32;
	if (temp_count == 0 || count > 16) return NoException;

	uint16_t ans = (*dest >> temp_count) | (reg << (16 - temp_count));

	uint8_t carry = (reg >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	set_zf_pf(env, ans);
	*dest = ans;
	return NoException;
}

BiosEmuExceptions shrd_32_32_8(
	BiosEmuEnvironment *env, uint32_t *dest, uint32_t reg, uint8_t count) {
	uint8_t temp_count = count % 32;
	if (temp_count == 0 || count > 32) return NoException;

	uint32_t ans = (*dest >> temp_count) | (reg << (32 - temp_count));

	uint8_t carry = (reg >> (temp_count - 1)) & 1;
	SET_CARRY_FLAG(carry);
	set_zf_pf(env, ans);
	*dest = ans;
	return NoException;
}

void cmps_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	int condition = 1;
	int i;
	for (i = repeat_times; i > 0 && condition; i--) {
		cmp_8(env, dst, *(uint8_t *)src);

		dst += delta_dst;
		src += delta_src;
		condition =
			((env->regs.flags >> ZeroFlagBit) & 1) == env->flags.rep_e_ne;
	}
	env->regs.cx = (env->flags.repeat) ? i : env->regs.cx;
}

void cmps_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	int condition = 1;
	int i;
	for (i = repeat_times; i > 0 && condition; i--) {
		cmp_32(env, dst, *(uint16_t *)src);

		dst += delta_dst;
		src += delta_src;
		condition =
			((env->regs.flags >> ZeroFlagBit) & 1) == env->flags.rep_e_ne;
	}
	env->regs.cx = (env->flags.repeat) ? i : env->regs.cx;
}

void cmps_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	int condition = 1;
	int i;
	for (i = repeat_times; i > 0 && condition; i--) {
		cmp_32(env, dst, *(uint32_t *)src);

		dst += delta_dst;
		src += delta_src;
		condition =
			((env->regs.flags >> ZeroFlagBit) & 1) == env->flags.rep_e_ne;
	}
	env->regs.ecx = (env->flags.repeat) ? i : env->regs.ecx;
}
