#ifndef _BIOS_EMU_MOD_RM_H
#define _BIOS_EMU_MOD_RM_H

#include "segment.h"
#include <bios_emu/environment.h>
#include <stdint.h>

// MODRM REG字段
#define MODRM_REG(x) ((x) << 3)

#define REG_AL (0b000)
#define REG_CL (0b001)
#define REG_DL (0b010)
#define REG_BL (0b011)
#define REG_AH (0b100)
#define REG_CH (0b101)
#define REG_DH (0b110)
#define REG_BH (0b111)

#define REG_AX REG_AL
#define REG_CX REG_CL
#define REG_DX REG_DL
#define REG_BX REG_BL
#define REG_SP REG_AH
#define REG_BP REG_CH
#define REG_SI REG_DH
#define REG_DI REG_BH

#define REG_EAX REG_AX
#define REG_ECX REG_CX
#define REG_EDX REG_DX
#define REG_EBX REG_BX
#define REG_ESP REG_SP
#define REG_EBP REG_BP
#define REG_ESI REG_SI
#define REG_EDI REG_DI

// MODRM MOD字段
#define MOD_BASE		0b00
#define MOD_BASE_DISP8	0b01
#define MOD_BASE_DISP16 0b10
#define MOD_BASE_DISP32 MOD_BASE_DISP16
#define MOD_REG			0b11

// MODRM RM字段
// 16位
// 1.base mod
#define RM_BX_SI (0b000)
#define RM_BX_DI (0b001)
#define RM_BP_SI (0b010)
#define RM_BP_DI (0b011)
#define RM_SI	 (0b100)
#define RM_DI	 (0b101)
#define RM_BP	 (0b110)
#define RM_BX	 (0b111)

// 4.reg mod
#define RM_REG_AL (0b000)
#define RM_REG_CL (0b001)
#define RM_REG_DL (0b010)
#define RM_REG_BL (0b011)
#define RM_REG_AH (0b100)
#define RM_REG_CH (0b101)
#define RM_REG_DH (0b110)
#define RM_REG_BH (0b111)

#define RM_REG_AX RM_REG_AL
#define RM_REG_CX RM_REG_CL
#define RM_REG_DX RM_REG_DL
#define RM_REG_BX RM_REG_BL
#define RM_REG_SP RM_REG_AH
#define RM_REG_BP RM_REG_CH
#define RM_REG_SI RM_REG_DH
#define RM_REG_DI RM_REG_BH

#define RM_REG_EAX RM_REG_AX
#define RM_REG_ECX RM_REG_CX
#define RM_REG_EDX RM_REG_DX
#define RM_REG_EBX RM_REG_BX
#define RM_REG_ESP RM_REG_SP
#define RM_REG_EBP RM_REG_BP
#define RM_REG_ESI RM_REG_SI
#define RM_REG_EDI RM_REG_DI

// 32位
// 1.base mod
#define RM_EAX (0b000)
#define RM_ECX (0b001)
#define RM_EDX (0b010)
#define RM_EBX (0b011)
#define RM_EBP (0b101)
#define RM_ESI (0b110)
#define RM_EDI (0b111)

#define RM_SREG(env, reg)        \
	({                           \
		void *var;               \
		switch (reg & 0x07) {    \
		case 0:                  \
			var = &env->regs.es; \
			break;               \
		case 1:                  \
			var = &env->regs.cs; \
			break;               \
		case 2:                  \
			var = &env->regs.ss; \
			break;               \
		case 3:                  \
			var = &env->regs.ds; \
			break;               \
		case 4:                  \
			var = &env->regs.fs; \
			break;               \
		case 5:                  \
			var = &env->regs.gs; \
			break;               \
		}                        \
		var;                     \
	})

#define RM_ADDR(env, modrm)                                                    \
	({                                                                         \
		void   *p;                                                             \
		uint8_t mod = modrm >> 6;                                              \
		if (mod == 0b11) {                                                     \
			if (env->flags.operand_size == 0) {                                \
				p = env->reg_lut_r16[modrm & 0b111];                           \
			} else {                                                           \
				p = env->reg_lut_r32[modrm & 0b111];                           \
			}                                                                  \
		} else {                                                               \
			uint32_t segment_base = get_segment_base(env, *env->default_ss);   \
			if (env->flags.address_size == 0) {                                \
				p = (void *)(segment_base + decode_rm_address_16(env, modrm)); \
			} else {                                                           \
				p = (void *)(segment_base + decode_rm_address_32(env, modrm)); \
			}                                                                  \
		}                                                                      \
		p;                                                                     \
	})

#define RM_ADDR16(env, modrm)                                                  \
	({                                                                         \
		void   *p;                                                             \
		uint8_t mod = modrm >> 6;                                              \
		if (mod == 0b11) {                                                     \
			p = env->reg_lut_r16[modrm & 0b111];                               \
		} else {                                                               \
			uint32_t segment_base = get_segment_base(env, *env->default_ss);   \
			if (env->flags.address_size == 0) {                                \
				p = (void *)(segment_base + decode_rm_address_16(env, modrm)); \
			} else {                                                           \
				p = (void *)(segment_base + decode_rm_address_32(env, modrm)); \
			}                                                                  \
		}                                                                      \
		p;                                                                     \
	})

#define RM_ADDR32(env, modrm)                                                  \
	({                                                                         \
		void   *p;                                                             \
		uint8_t mod = modrm >> 6;                                              \
		if (mod == 0b11) {                                                     \
			p = env->reg_lut_r32[modrm & 0b111];                               \
		} else {                                                               \
			uint32_t segment_base = get_segment_base(env, *env->default_ss);   \
			if (env->flags.address_size == 0) {                                \
				p = (void *)(segment_base + decode_rm_address_16(env, modrm)); \
			} else {                                                           \
				p = (void *)(segment_base + decode_rm_address_32(env, modrm)); \
			}                                                                  \
		}                                                                      \
		p;                                                                     \
	})

size_t decode_rm_address_16(BiosEmuEnvironment *env, uint8_t modrm);
size_t decode_rm_address_32(BiosEmuEnvironment *env, uint8_t modrm);
size_t decode_rm_address(BiosEmuEnvironment *env, uint8_t modrm);

#endif