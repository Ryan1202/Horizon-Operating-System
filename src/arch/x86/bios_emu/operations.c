#include "includes/operations.h"
#include "bios_emu/exceptions.h"
#include "includes/alu.h"
#include "includes/decode.h"
#include "includes/mod_rm.h"
#include "includes/segment.h"
#include "includes/stack.h"
#include "kernel/func.h"
#include <bios_emu/environment.h>
#include <stdint.h>

int ptr_within_code_segment_limit(BiosEmuEnvironment *env, uint32_t address) {
	// uint32_t segment = env->regs.cs;
	// if (env->flags.address_size == 0) {
	// 	return (address < (segment << 4) + 0xffff) && address >= (segment << 4);
	// }
	return 1;
}

BiosEmuExceptions decode_0x80_0x83(BiosEmuEnvironment *env, uint8_t opcode) {
	static const CalcIndex index_table[8] = {
		/* 0b000 */ CALC_ADD,
		/* 0b001 */ CALC_OR,
		/* 0b010 */ CALC_ADC,
		/* 0b011 */ CALC_SBB,
		/* 0b100 */ CALC_AND,
		/* 0b101 */ CALC_SUB,
		/* 0b110 */ CALC_XOR,
		/* 0b111 */ CALC_CMP};
	uint8_t	  modrm = *(uint8_t *)env->cur_ip;
	uint8_t	  sel	= (modrm >> 3) & 0b111;
	CalcIndex index = index_table[sel & 0b111];

	switch (opcode & 0b11) {
	case 0b00:
		calc_rm_imm_8(env, index);
		break;
	case 0b01:
		calc_rm_imm(env, index);
		break;
	case 0b10:
		return InvalidOpcode;
	case 0b11:
		calc_rm_imm8(env, index);
		break;
	}
	return NoException;
}

BiosEmuExceptions decode_0xba(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	uint8_t sel	  = (modrm >> 3) & 0b111;

	switch (sel) {
	case 0b100:
		decode_rm_imm8(env, bt_16_16, bt_32_32);
		break;
	case 0b101:
		decode_rm_imm8(env, bts_16_16, bts_32_32);
		break;
	case 0b110:
		decode_rm_imm8(env, btr_16_16, btr_32_32);
		break;
	case 0b111:
		decode_rm_imm8(env, btc_16_16, btc_32_32);
		break;
	default:
		env->cur_ip--;
		env->regs.eip--;
		return InvalidOpcode;
	}
	return NoException;
}

void (*rotate_shift_8[8])(
	BiosEmuEnvironment *env, uint8_t *value, uint8_t count) = {
	rol_8, ror_8, rcl_8, rcr_8, shl_sal_8, shr_8, shl_sal_8, sar_8};
void (*rotate_shift16[8])(
	BiosEmuEnvironment *env, uint16_t *value, uint8_t count) = {
	rol_16, ror_16, rcl_16, rcr_16, shl_sal_16, shr_16, shl_sal_16, sar_16};
void (*rotate_shift32[8])(
	BiosEmuEnvironment *env, uint32_t *value, uint8_t count) = {
	rol_32, ror_32, rcl_32, rcr_32, shl_sal_32, shr_32, shl_sal_32, sar_32};

BiosEmuExceptions decode_0xc0(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	rotate_shift_8[sel](env, RM_ADDR(env, modrm), *(uint8_t *)env->cur_ip);
	env->regs.eip++;
	env->cur_ip++;

	return NoException;
}

BiosEmuExceptions decode_0xc1(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		rotate_shift16[sel](env, RM_ADDR(env, modrm), *(uint8_t *)env->cur_ip);
		env->regs.eip++;
		env->cur_ip++;
	} else {
		rotate_shift32[sel](env, RM_ADDR(env, modrm), *(uint8_t *)env->cur_ip);
		env->regs.eip++;
		env->cur_ip++;
	}

	return NoException;
}

BiosEmuExceptions decode_0xd0(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	rotate_shift_8[sel](env, RM_ADDR(env, modrm), 1);

	return NoException;
}

BiosEmuExceptions decode_0xd1(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		rotate_shift16[sel](env, RM_ADDR(env, modrm), 1);
	} else {
		rotate_shift32[sel](env, RM_ADDR(env, modrm), 1);
	}

	return NoException;
}

BiosEmuExceptions decode_0xd2(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	rotate_shift_8[sel](env, RM_ADDR(env, modrm), env->regs.cl);

	return NoException;
}

BiosEmuExceptions decode_0xd3(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		rotate_shift16[sel](env, RM_ADDR(env, modrm), env->regs.cl);
	} else {
		rotate_shift32[sel](env, RM_ADDR(env, modrm), env->regs.cl);
	}

	return NoException;
}

BiosEmuExceptions decode_0xf6(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	if (sel == 0) {
		env->cur_ip--;
		env->regs.eip--;
		calc_rm_imm8(env, CALC_TEST);
		return NoException;
	}

	void	*p	  = RM_ADDR(env, modrm);
	uint8_t *addr = env->flags.operand_size == 0
					  ? (uint8_t *)((env->regs.cs << 4) + (size_t)p)
					  : p;

	BiosEmuExceptions exception = NoException;
	switch (sel) {
	case 0b010:
		*addr = ~(*addr);
		break;
	case 0b011:
		neg_8(env, addr);
		break;
	case 0b100:
		mul_8(env, *addr);
		break;
	case 0b101:
		imul_8(env, *addr);
		break;
	case 0b110:
		exception = div_8(env, *addr);
		break;
	case 0b111:
		exception = idiv_8(env, *addr);
		break;
	}
	return exception;
}

BiosEmuExceptions decode_0xf7(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	uint8_t sel = (modrm >> 3) & 0b111;

	uint8_t *p = (uint8_t *)RM_ADDR(env, modrm);

	BiosEmuExceptions exception = NoException;
	switch (sel) {
	case 0b000:
		env->cur_ip--;
		env->regs.eip--;
		calc_rm_imm(env, CALC_TEST);
		break;
	case 0b010:
		if (env->flags.operand_size == 0) {
			*(uint16_t *)p = ~(*(uint16_t *)p);
		} else {
			*(uint32_t *)p = ~(*(uint32_t *)p);
		}
		break;
	case 0b011:
		if (env->flags.operand_size == 0) {
			neg_16(env, (uint16_t *)p);
		} else {
			neg_32(env, (uint32_t *)p);
		}
		break;
	case 0b100:
		if (env->flags.operand_size == 0) {
			mul_16(env, *(uint16_t *)p);
		} else {
			mul_32(env, *(uint32_t *)p);
		}
		break;
	case 0b101:
		if (env->flags.operand_size == 0) {
			imul_16(
				env, &env->regs.dx, &env->regs.ax, env->regs.ax,
				*(uint16_t *)p);
		} else {
			imul_32(
				env, &env->regs.edx, &env->regs.eax, env->regs.eax,
				*(uint16_t *)p);
		}
		break;
	case 0b110:
		exception = !env->flags.operand_size ? div_16(env, *(uint16_t *)p)
											 : div_32(env, *(uint32_t *)p);
		break;
	case 0b111:
		exception = !env->flags.operand_size ? idiv_16(env, *(uint16_t *)p)
											 : idiv_32(env, *(uint32_t *)p);
		break;
	}
	return exception;
}

BiosEmuExceptions decode_0xfe(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	switch ((modrm >> 3) & 0b111) {
	case 0b000:
		// INC r/m
		decode_inc_rm8(env);
		break;
	case 0b001:
		// DEC r/m
		decode_dec_rm8(env);
		break;
	default:
		env->regs.eip--;
		env->cur_ip--;
		return InvalidOpcode;
	}
	return NoException;
}

BiosEmuExceptions decode_0xff(BiosEmuEnvironment *env) {
	BiosEmuExceptions exception = NoException;
	uint8_t			  modrm		= *(uint8_t *)env->cur_ip++;
	uint8_t			  sel		= (modrm >> 3) & 0b111;
	env->regs.eip++;
	switch (sel) {
	case 0b000:
		// INC r/m
		decode_inc_rm(env);
		break;
	case 0b001:
		// DEC r/m
		decode_dec_rm(env);
		break;
	case 0b010:
		// CALL r/m
		if (modrm >> 6 == 0b11) {
			if (env->flags.operand_size == 0) {
				uint16_t *reg = env->reg_lut_r16[modrm & 0b111];
				decode_call_near(env, *reg);
			} else {
				uint32_t *reg = env->reg_lut_r32[modrm & 0b111];
				decode_call_near(env, *reg);
			}
		} else {
			if (env->flags.operand_size == 0) {
				uint16_t address = decode_rm_address_16(env, modrm);
				address = fetch_data_16(env, *env->default_ss, address);
				decode_call_near(env, address);
			} else {
				uint32_t address = decode_rm_address_32(env, modrm);
				address = fetch_data_32(env, *env->default_ss, address);
				decode_call_near(env, address);
			}
		}
		break;
	case 0b011:
		// CALL m16:16/32
		if (env->flags.operand_size == 0) {
			uint16_t address = decode_rm_address_16(env, modrm);
			address			 = fetch_data_16(env, *env->default_ss, address);
			uint16_t segment =
				fetch_data_16(env, *env->default_ss, address + 2);
			decode_call_far(env, segment, address);
		} else {
			uint32_t address = decode_rm_address_32(env, modrm);
			address			 = fetch_data_32(env, *env->default_ss, address);
			uint32_t segment =
				fetch_data_32(env, *env->default_ss, address + 4);
			decode_call_far(env, segment, address);
		}
		break;
	case 0b100:
		// JMP r/m
		if (modrm >> 6 == 0b11) {
			if (env->flags.operand_size == 0) {
				uint16_t *reg = env->reg_lut_r16[modrm & 0b111];
				decode_jmp_near(env, *reg);
			} else {
				uint32_t *reg = env->reg_lut_r32[modrm & 0b111];
				decode_jmp_near(env, *reg);
			}
		} else {
			if (env->flags.operand_size == 0) {
				uint16_t address = decode_rm_address_16(env, modrm);
				address = fetch_data_16(env, *env->default_ss, address);
				decode_jmp_near(env, (int16_t)address);
			} else {
				uint32_t address = decode_rm_address_32(env, modrm);
				address = fetch_data_32(env, *env->default_ss, address);
				decode_jmp_near(env, (int32_t)address);
			}
		}
		break;
	case 0b101:
		// Long JMP
		if (env->flags.operand_size == 0) {
			uint16_t address = decode_rm_address_16(env, modrm);
			address			 = fetch_data_16(env, *env->default_ss, address);
			uint16_t segment =
				fetch_data_16(env, *env->default_ss, address + 2);
			decode_jmp_far(env, segment, address);
		} else {
			uint32_t address = decode_rm_address_32(env, modrm);
			address			 = fetch_data_32(env, *env->default_ss, address);
			uint32_t segment =
				fetch_data_32(env, *env->default_ss, address + 4);
			decode_jmp_far(env, segment, address);
		}
		break;
	case 0b110:
		// PUSH r/m16
		if (modrm >> 6 == 0b11) {
			// PUSH r16
			if (env->flags.operand_size == 0) {
				uint16_t *reg = env->reg_lut_r16[modrm & 0b111];
				PUSH(env, *reg, 2);
			} else {
				uint32_t *reg = env->reg_lut_r32[modrm & 0b111];
				PUSH(env, *reg, 4);
			}
		} else {
			// PUSH [r/m16]
			if (env->flags.operand_size == 0) {
				uint16_t val = decode_rm_address_16(env, modrm);
				val			 = fetch_data_16(env, *env->default_ss, val);
				PUSH(env, val, 2);
			} else {
				uint32_t val = decode_rm_address_32(env, modrm);
				val			 = fetch_data_32(env, *env->default_ss, val);
				PUSH(env, val, 4);
			}
		}
		break;
	default:
		env->regs.eip--;
		env->cur_ip--;
		exception = InvalidOpcode;
	}
	return exception;
}

void decode_in8(BiosEmuEnvironment *env, uint8_t port) {
	env->regs.al = io_in8(port);
}

void decode_in(BiosEmuEnvironment *env, uint16_t port) {
	if (env->flags.operand_size == 0) {
		env->regs.ax = io_in16(port);
	} else {
		env->regs.eax = io_in32(port);
	}
}

void decode_out8(BiosEmuEnvironment *env, uint8_t port) {
	io_out8(port, env->regs.al);
}

void decode_out(BiosEmuEnvironment *env, uint16_t port) {
	if (env->flags.operand_size == 0) {
		io_out16(port, env->regs.ax);
	} else {
		io_out32(port, env->regs.eax);
	}
}

void ins_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep insb;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.cx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void ins_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep insw;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.cx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void ins_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep insl;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.ecx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void outs_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep outsb;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.cx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void outs_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep outsw;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.cx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void outs_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times) {
	__asm__ volatile("cld;"
					 "rep outsl;"
					 :
					 : "D"(dst), "S"(src), "c"(env->regs.ecx), "d"(env->regs.dx)
					 : "cc", "memory");
}

void decode_lea(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	size_t address;
	if (env->flags.address_size == 0) {
		address = decode_rm_address_16(env, modrm);
	} else {
		address = decode_rm_address_32(env, modrm);
	}
	if (env->flags.operand_size == 0) {
		uint16_t *dst = env->reg_lut_r16[reg];
		*dst		  = (uint16_t)address;
	} else {
		uint32_t *dst = env->reg_lut_r32[reg];
		*dst		  = (uint32_t)address;
	}
	return;
}

void decode_setcc(BiosEmuEnvironment *env, int condition) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	uint8_t *addr = RM_ADDR(env, modrm);
	*addr		  = condition ? 1 : 0;
	return;
}

void decode_cbw_cwde(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		env->regs.ax = (int16_t)(int8_t)env->regs.al;
	} else {
		env->regs.eax = (int32_t)(int16_t)env->regs.ax;
	}
	return;
}

void decode_cwd_cdq(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		env->regs.dx = ((int32_t)(int16_t)env->regs.ax) >> 16;
	} else {
		env->regs.edx = (env->regs.eax >> 31) ? 0xffffffff : 0;
	}
	return;
}
