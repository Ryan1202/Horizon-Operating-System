#include "../includes/decode.h"
#include "../includes/flags.h"
#include "../includes/mod_rm.h"
#include "../includes/operations.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

void decode_mov_rm_r8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *dst = RM_ADDR(env, modrm);
	uint8_t *src = env->reg_lut_r8[reg];
	*dst		 = *src;
	return;
}

void decode_mov_rm_r(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = RM_ADDR16(env, modrm);
		uint16_t *src = env->reg_lut_r16[reg];
		*dst		  = *src;
	} else {
		uint32_t *dst = RM_ADDR32(env, modrm);
		uint32_t *src = env->reg_lut_r32[reg];
		*dst		  = *src;
	}

	return;
}

void decode_mov_r_rm_8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *dst = env->reg_lut_r8[reg];
	uint8_t *src = RM_ADDR(env, modrm);
	*dst		 = *src;
	return;
}

void decode_mov_r_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = env->reg_lut_r16[reg];
		uint32_t *src = RM_ADDR16(env, modrm);
		*dst		  = *src;
	} else {
		uint32_t *dst = env->reg_lut_r32[reg];
		uint32_t *src = RM_ADDR32(env, modrm);
		*dst		  = *src;
	}

	return;
}

void decode_mov_rm_sreg(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	void *dst = RM_ADDR(env, modrm);
	if (env->flags.operand_size == 0) {
		uint16_t *src	 = RM_SREG(env, reg);
		*(uint16_t *)dst = *src;
	} else {
		uint32_t *src	 = RM_SREG(env, reg);
		*(uint32_t *)dst = *src;
	}
	return;
}

void decode_mov_sreg_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint16_t *dst = RM_SREG(env, reg);
	uint16_t *src = RM_ADDR(env, modrm);
	*dst		  = *src;
	return;
}

void decode_mov_r_moffs_8(BiosEmuEnvironment *env) {
	uint32_t segment = *env->default_ss;

	void	*src = (void *)get_segment_base(env, segment);
	uint32_t offset;
	if (env->flags.address_size == 0) {
		offset = *(uint16_t *)env->cur_ip;
		env->regs.eip += 2;
		env->cur_ip += 2;
	} else {
		offset = *(uint32_t *)env->cur_ip;
		env->regs.eip += 4;
		env->cur_ip += 4;
	}
	src			 = (void *)((size_t)src + offset);
	env->regs.al = *(uint8_t *)src;
	return;
}

void decode_mov_r_moffs(BiosEmuEnvironment *env) {
	uint32_t segment = *env->default_ss;

	void	*src = (void *)get_segment_base(env, segment);
	uint32_t offset;
	if (env->flags.address_size == 0) {
		offset = *(uint16_t *)env->cur_ip;
		env->regs.eip += 2;
		env->cur_ip += 2;
	} else {
		offset = *(uint32_t *)env->cur_ip;
		env->regs.eip += 4;
		env->cur_ip += 4;
	}
	src = (void *)((size_t)src + offset);
	if (env->flags.operand_size == 0) env->regs.ax = *(uint16_t *)src;
	else env->regs.eax = *(uint32_t *)src;
	return;
}

void decode_mov_moffs_r_8(BiosEmuEnvironment *env) {
	uint32_t segment = *env->default_ss;

	void	*dst = (void *)get_segment_base(env, segment);
	uint32_t offset;
	if (env->flags.address_size == 0) {
		offset = *(uint16_t *)env->cur_ip;
		env->regs.eip += 2;
		env->cur_ip += 2;
	} else {
		offset = *(uint32_t *)env->cur_ip;
		env->regs.eip += 4;
		env->cur_ip += 4;
	}
	dst = (void *)((size_t)dst + offset);

	*(uint8_t *)dst = env->regs.al;
	return;
}

void decode_mov_moffs_r(BiosEmuEnvironment *env) {
	uint32_t segment = *env->default_ss;

	void	*dst = (void *)get_segment_base(env, segment);
	uint32_t offset;
	if (env->flags.address_size == 0) {
		offset = *(uint16_t *)env->cur_ip;
		env->regs.eip += 2;
		env->cur_ip += 2;
	} else {
		offset = *(uint32_t *)env->cur_ip;
		env->regs.eip += 4;
		env->cur_ip += 4;
	}
	dst = (void *)((size_t)dst + offset);
	if (env->flags.operand_size == 0) *(uint16_t *)dst = env->regs.ax;
	else *(uint32_t *)dst = env->regs.eax;
	return;
}

void decode_mov_r_imm8(BiosEmuEnvironment *env, uint8_t opcode) {
	uint8_t *reg = PLUS_RB_REG(env, opcode);
	*reg		 = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
}

void decode_mov_r_imm(BiosEmuEnvironment *env, uint8_t opcode) {
	if (env->flags.operand_size == 0) {
		uint16_t *reg = PLUS_RW_REG(env, opcode);
		*reg		  = *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
	} else {
		uint32_t *reg = PLUS_RD_REG(env, opcode);
		*reg		  = *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
	}
}

void decode_mov_rm_imm8(BiosEmuEnvironment *env) {
	uint8_t *val;
	uint8_t	 modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	val	 = RM_ADDR(env, modrm);
	*val = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
}

void decode_mov_rm_imm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	if (env->flags.operand_size == 0) {
		uint16_t *val = RM_ADDR16(env, modrm);

		*val = *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
	} else {
		uint32_t *val = RM_ADDR32(env, modrm);

		*val = *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
	}
}

void decode_movzx_r_rm8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = env->reg_lut_r16[reg];
		uint8_t	 *src = RM_ADDR16(env, modrm);
		*dst		  = (uint16_t)*src;
	} else {
		uint32_t *dst = env->reg_lut_r32[reg];
		uint8_t	 *src = RM_ADDR32(env, modrm);
		*dst		  = (uint32_t)*src;
	}
	return;
}

void decode_movzx_r_rm16(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint32_t *dst = env->reg_lut_r32[reg];
	uint16_t *src = RM_ADDR32(env, modrm);
	*dst		  = (uint32_t)*src;
	return;
}

void decode_movsx_r_rm8(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		uint16_t *dst = env->reg_lut_r16[reg];
		int8_t	 *src = RM_ADDR16(env, modrm);
		*dst		  = (int16_t)*src;
	} else {
		uint32_t *dst = env->reg_lut_r32[reg];
		int8_t	 *src = RM_ADDR32(env, modrm);
		*dst		  = (int32_t)*src;
	}
	return;
}

void decode_movsx_r_rm16(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint32_t *dst = env->reg_lut_r32[reg];
	int16_t	 *src = RM_ADDR32(env, modrm);
	*dst		  = (int32_t)*src;
	return;
}

void movs_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	for (int i = 0; i < repeat_times; i++) {
		*(uint8_t *)dst = *(uint8_t *)src;

		dst += delta_dst;
		src += delta_src;
	}
	return;
}

void movs_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	for (int i = 0; i < repeat_times; i++) {
		*(uint16_t *)dst = *(uint16_t *)src;

		dst += delta_dst;
		src += delta_src;
	}
}

void movs_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	for (int i = 0; i < repeat_times; i++) {
		*(uint32_t *)dst = *(uint32_t *)src;

		dst += delta_dst;
		src += delta_src;
	}
}
