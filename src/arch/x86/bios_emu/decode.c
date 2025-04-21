#include "includes/decode.h"
#include "includes/alu.h"
#include "includes/conditions.h"
#include "includes/instructions_1.h"
#include "includes/instructions_2.h"
#include "includes/mod_rm.h"
#include "includes/operations.h"
#include "includes/prefix.h"
#include "includes/segment.h"
#include "includes/stack.h"
#include "kernel/func.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

BiosEmuExceptions nop_16(BiosEmuEnvironment *env, uint16_t *addr1) {
	return NoException;
}

BiosEmuExceptions decode_r(
	BiosEmuEnvironment *env, Op1_16 func16, Op1_32 func32) {
	uint8_t reg = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	if (env->flags.operand_size == 0) {
		uint16_t *r16 = env->reg_lut_r16[reg];
		return func16(env, r16);
	} else {
		uint32_t *r32 = env->reg_lut_r32[reg];
		return func32(env, r32);
	}
}

BiosEmuExceptions decode_rm8_r8(BiosEmuEnvironment *env, Op2_8_8 func) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *rm = RM_ADDR(env, modrm);
	uint8_t *r8 = env->reg_lut_r8[reg];

	return func(env, rm, r8);
}

BiosEmuExceptions decode_rm_r(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	void *rm = RM_ADDR(env, modrm);
	if (env->flags.operand_size == 0) {
		uint16_t *r16 = env->reg_lut_r16[reg];
		return func16(env, rm, r16);
	} else {
		uint32_t *r32 = env->reg_lut_r32[reg];
		return func32(env, rm, r32);
	}
}

BiosEmuExceptions decode_rm_imm8(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	void	*rm	   = RM_ADDR(env, modrm);
	uint32_t value = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	if (env->flags.operand_size == 0) {
		return func16(env, rm, (uint16_t *)&value);
	} else {
		return func32(env, rm, &value);
	}
}

BiosEmuExceptions decode_r8_rm8(BiosEmuEnvironment *env, Op2_8_8 func) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	uint8_t *rm = RM_ADDR(env, modrm);
	uint8_t *r8 = env->reg_lut_r8[reg];

	return func(env, r8, rm);
}

BiosEmuExceptions decode_r_rm(
	BiosEmuEnvironment *env, Op2_16_16 func16, Op2_32_32 func32) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	if (env->flags.operand_size == 0) {
		void *r = env->reg_lut_r16[reg];
		return func16(env, r, RM_ADDR(env, modrm));
	} else {
		void *r = env->reg_lut_r32[reg];
		return func32(env, r, RM_ADDR(env, modrm));
	}
}

BiosEmuExceptions decode_rm_r_imm8(
	BiosEmuEnvironment *env, Op3_16_16_8 func16, Op3_32_32_8 func32) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	void *rm = RM_ADDR(env, modrm);
	if (env->flags.operand_size == 0) {
		uint16_t *r16 = env->reg_lut_r16[reg];
		return func16(env, rm, *r16, *(uint8_t *)env->cur_ip++);
	} else {
		uint32_t *r32 = env->reg_lut_r32[reg];
		return func32(env, rm, *r32, *(uint8_t *)env->cur_ip++);
	}
}

BiosEmuExceptions decode_rm_r_cl(
	BiosEmuEnvironment *env, Op3_16_16_8 func16, Op3_32_32_8 func32) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;
	uint8_t reg = (modrm >> 3) & 0b111;

	void *rm = RM_ADDR(env, modrm);
	if (env->flags.operand_size == 0) {
		uint16_t *r16 = env->reg_lut_r16[reg];
		return func16(env, rm, *r16, env->regs.cl);
	} else {
		uint32_t *r32 = env->reg_lut_r32[reg];
		return func32(env, rm, *r32, env->regs.cl);
	}
}

void decode_string_instructions_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	OpStr opstr_8) {
	int repeat_times;

	if (env->regs.flags & BIT(DirectionFlagBit)) {
		delta_dst = -delta_dst;
		delta_src = -delta_src;
	}

	if (env->flags.operand_size == 0) {
		repeat_times = (env->flags.repeat) ? env->regs.cx : 1;
	} else {
		repeat_times = (env->flags.repeat) ? env->regs.ecx : 1;
	}
	opstr_8(env, dst, delta_dst, src, delta_src, repeat_times);
	return;
}

void decode_string_instructions_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	OpStr opstr_16, OpStr opstr_32) {
	int repeat_times;

	if (env->regs.flags & BIT(DirectionFlagBit)) {
		delta_dst = -delta_dst;
		delta_src = -delta_src;
	}

	if (env->flags.operand_size == 0) {
		repeat_times = (env->flags.repeat) ? env->regs.cx : 1;
		delta_dst <<= 1;
		opstr_16(env, dst, delta_dst, src, delta_src, repeat_times);
	} else {
		repeat_times = (env->flags.repeat) ? env->regs.ecx : 1;
		delta_dst <<= 2;
		opstr_32(env, dst, delta_dst, src, delta_src, repeat_times);
	}
	return;
}

BiosEmuExceptions decode_two_bytes_opcode(BiosEmuEnvironment *env) {
	TwoBytesOpcodes *opcode = env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	switch ((uint8_t)*opcode) {
	case OP_BSF_TZCNT:
		decode_rm_r(env, bsf_16_16, bsf_32_32);
		break;
	case OP_BSR_LZCNT:
		decode_rm_r(env, bsr_16_16, bsr_32_32);
		break;
	case OP_BSWAP:
		env->flags.operand_size = 1; // BSWAP只有32位操作数
		decode_r(env, nop_16, bswap_32);
		break;
	case OP_BT:
		decode_rm_r(env, bt_16_16, bt_32_32);
		break;
	case OP_BTC:
		decode_rm_r(env, btc_16_16, btc_32_32);
		break;
	case OP_BTR:
		decode_rm_r(env, btr_16_16, btr_32_32);
		break;
	case OP_BTS:
		decode_rm_r(env, bts_16_16, bts_32_32);
		break;
	case OP_CMPXCHG8:
		decode_rm8_r8(env, cmpxchg_8_8);
		break;
	case OP_CMPXCHG:
		decode_rm_r(env, cmpxchg_16_16, cmpxchg_32_32);
		break;
	case 0xba:
		decode_0xba(env);
		break;
	case OP_IMUL_r_rm:
		decode_imul_r_rm(env);
		break;
	case OP_Jcc ...(OP_Jcc + 15):
		decode_jcc(env, condition_table[*opcode & 0x0f](env));
		break;
	case OP_LSS:
		decode_rm_r(env, lss_16_16, lss_32_32);
		break;
	case OP_LFS:
		decode_rm_r(env, lfs_16_16, lfs_32_32);
		break;
	case OP_LGS:
		decode_rm_r(env, lgs_16_16, lgs_32_32);
		break;
	case OP_MOVZX_r_rm8:
		decode_movzx_r_rm8(env);
		break;
	case OP_MOVZX_r_rm16:
		decode_movzx_r_rm16(env);
		break;
	case OP_MOVSX_r_rm8:
		decode_movsx_r_rm8(env);
		break;
	case OP_MOVSX_r_rm16:
		decode_movsx_r_rm16(env);
		break;
	case OP_PUSH_FS:
		PUSH_SREG(env, fs);
		break;
	case OP_PUSH_GS:
		PUSH_SREG(env, gs);
		break;
	case OP_POP_FS:
		POP_SREG(env, fs);
		break;
	case OP_POP_GS:
		POP_SREG(env, gs);
		break;
	case OP_SETcc ...(OP_SETcc + 15):
		decode_setcc(env, condition_table[*opcode & 0x0f](env));
		break;
	case OP_SHLD_imm8:
		decode_rm_r_imm8(env, shld_16_16_8, shld_32_32_8);
		break;
	case OP_SHLD_cl:
		decode_rm_r_cl(env, shld_16_16_8, shld_32_32_8);
		break;
	case OP_SHRD_imm8:
		decode_rm_r_imm8(env, shrd_16_16_8, shrd_32_32_8);
		break;
	case OP_SHRD_cl:
		decode_rm_r_cl(env, shrd_16_16_8, shrd_32_32_8);
		break;
	case OP_XADD_rm_r_8:
		decode_rm8_r8(env, xadd_8_8);
		break;
	case OP_XADD_rm_r:
		decode_rm_r(env, xadd_16_16, xadd_32_32);
		break;
	default:
		return InvalidOpcode;
	}
	return NoException;
}

BiosEmuExceptions decode_one_byte_opcode(BiosEmuEnvironment *env) {
	BiosEmuExceptions exception = NoException;
	OneByteOpcodes	 *opcode	= env->cur_ip;
	env->cur_ip++;
	env->regs.eip++;
	switch ((uint8_t)*opcode) {
	case OP_AAA:
		decode_aaa(env);
		break;
	case OP_AAD:
		decode_aad(env);
		break;
	case OP_AAM:
		decode_aam(env);
		break;
	case OP_AAS:
		decode_aas(env);
		break;
	case OP_BOUND:
		exception = decode_r_rm(env, bound_16_16, bound_32_32);
		break;
	case OP_CALL:
		exception = decode_call(env);
		break;
	case OP_CALL_ptr:
		exception = decode_call_ptr(env);
		break;
	case OP_CBW_CWDE:
		decode_cbw_cwde(env);
		break;
	case OP_CLC:
		CLC(env);
		break;
	case OP_CLD:
		CLD(env);
		break;
	case OP_CLI:
		CLI(env);
		io_cli();
		break;
	case OP_CMC:
		CMC(env);
		break;
	case OP_CMPS8: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = GET_REG_POINTER(env, es, di);
		decode_string_instructions_8(env, dst, 1, src, 1, cmps_8);
		break;
	}
	case OP_CMPS: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = GET_REG_POINTER(env, es, di);
		decode_string_instructions_16(env, dst, 1, src, 1, cmps_16, cmps_32);
		break;
	}
	case OP_CWD_CDQ:
		decode_cwd_cdq(env);
		break;
	case OP_ENTER:
		exception = decode_enter(env);
		break;
	case OP_DAA:
		decode_daa(env);
		break;
	case OP_DAS:
		decode_das(env);
		break;
	case OP_DEC ...(OP_DEC + 7):
		decode_dec_r(env, *opcode);
		break;
	case OP_HLT:
		exception = EventHalted;
		break;
	case OP_IMUL_imm8:
		decode_imul_r_rm_imm8(env);
		break;
	case OP_IMUL_imm16:
		decode_imul_r_rm_imm16(env);
		break;
	case OP_IN8:
		decode_in8(env, *(uint8_t *)env->cur_ip);
		env->cur_ip++;
		env->regs.eip++;
		break;
	case OP_IN:
		decode_in(env, *(uint8_t *)env->cur_ip);
		env->cur_ip += 2;
		env->regs.eip += 2;
		break;
	case OP_IN8_dx:
		decode_in8(env, env->regs.dx);
		break;
	case OP_IN_dx:
		decode_in(env, env->regs.dx);
		break;
	case OP_INS8: {
		void *src = &env->regs.dx;
		void *dst = GET_REG_POINTER(env, ds, si);
		decode_string_instructions_8(env, dst, 0, src, 1, ins_8);
		break;
	}
	case OP_INS: {
		void *src = &env->regs.dx;
		void *dst = GET_REG_POINTER(env, ds, si);
		decode_string_instructions_16(env, dst, 0, src, 1, ins_16, ins_32);
		break;
	}
	case OP_INC ...(OP_INC + 7):
		decode_inc_r(env, *opcode);
		break;
	case OP_INT:
		exception = decode_int(env, *(uint8_t *)env->cur_ip++);
		break;
	case OP_INTO:
		if (env->regs.flags & BIT(OverflowFlagBit)) {
			exception = decode_int(env, 4);
		}
		break;
	case OP_IRET_IRETD:
		exception = decode_iret(env);
		break;
	case OP_JE_JZ_8:
	case OP_JG_JNLE_8:
	case OP_JGE_JNL_8:
	case OP_JL_JNGE_8:
	case OP_JLE_JNG_8:
	case OP_JBE_JNA_8:
	case OP_JB_JC_JNAE_8:
	case OP_JAE_JNB_JNC_8:
	case OP_JA_JNBE_8:
	case OP_JNE_JNZ_8:
	case OP_JNO_8:
	case OP_JNP_JPO_8:
	case OP_JNS_8:
	case OP_JO_8:
	case OP_JP_JPE_8:
	case OP_JS_8:
		decode_jcc_8(env, condition_table[*opcode & 0x0f](env));
		break;
	case OP_JMP8:
		decode_jmp8(env);
		break;
	case OP_JMP:
		decode_jmp(env);
		break;
	case OP_LongJMP:
		decode_long_jmp_ptr16(env);
		break;
	case OP_LAHF:
		LAHF(env);
		break;
	case OP_LDS:
		decode_r_rm(env, lds_16_16, lds_32_32);
		break;
	case OP_LEA:
		exception = decode_lea(env);
		break;
	case OP_LEAVE:
		decode_leave(env);
		break;
	case OP_LES:
		decode_r_rm(env, les_16_16, les_32_32);
		break;
	case OP_LODS8: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = &env->regs.al;
		decode_string_instructions_8(env, dst, 0, src, 1, movs_8);
		break;
	}
	case OP_LODS: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = GET_REG_ADDR(env, ax);
		decode_string_instructions_16(env, dst, 0, src, 1, movs_16, movs_32);
		break;
	}
	case OP_LOOP: {
		uint32_t count;
		if (env->flags.operand_size == 0) {
			env->regs.cx--;
			count = env->regs.cx;
		} else {
			env->regs.ecx--;
			count = env->regs.ecx;
		}
		if (count) decode_jmp8(env);
		break;
	}
	case OP_LOOPE: {
		uint32_t count;
		if (env->flags.operand_size == 0) {
			env->regs.cx--;
			count = env->regs.cx;
		} else {
			env->regs.ecx--;
			count = env->regs.ecx;
		}
		if (count) decode_jcc(env, condition_table[4](env));
		break;
	}
	case OP_LOOPNE: {
		uint32_t count;
		if (env->flags.operand_size == 0) {
			env->regs.cx--;
			count = env->regs.cx;
		} else {
			env->regs.ecx--;
			count = env->regs.ecx;
		}
		if (count) decode_jcc(env, condition_table[5](env));
		break;
	}
	case OP_MOV_rm_r8:
		exception = decode_rm8_r8(env, mov_8_8);
		break;
	case OP_MOV_rm_r:
		exception = decode_rm_r(env, mov_16_16, mov_32_32);
		break;
	case OP_MOV_r_rm8:
		exception = decode_r8_rm8(env, mov_8_8);
		break;
	case OP_MOV_r_rm:
		exception = decode_r_rm(env, mov_16_16, mov_32_32);
		break;
	case OP_MOV_rm_sreg:
		decode_mov_rm_sreg(env);
		break;
	case OP_MOV_sreg_rm:
		decode_mov_sreg_rm(env);
		break;
	case OP_MOV_a_moffs8:
		decode_mov_r_moffs_8(env);
		break;
	case OP_MOV_a_moffs:
		decode_mov_r_moffs(env);
		break;
	case OP_MOV_moffs8_a:
		decode_mov_moffs_r_8(env);
		break;
	case OP_MOV_moffs_a:
		decode_mov_moffs_r(env);
		break;
	case OP_MOV_r_imm8 ...(OP_MOV_r_imm8 + 7):
		decode_mov_r_imm8(env, *opcode);
		break;
	case OP_MOV_r_imm ...(OP_MOV_r_imm + 7):
		decode_mov_r_imm(env, *opcode);
		break;
	case OP_MOV_rm_imm8:
		exception = decode_rm_imm8(env, mov_16_16, mov_32_32);
		break;
	case OP_MOV_rm_imm:
		decode_mov_rm_imm(env);
		break;
	case OP_MOVS8: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = GET_REG_POINTER(env, es, di);
		decode_string_instructions_8(env, dst, 1, src, 1, movs_8);
		break;
	}
	case OP_MOVS: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = GET_REG_POINTER(env, es, di);
		decode_string_instructions_16(env, dst, 1, src, 1, movs_16, movs_32);
		break;
	}
	case OP_NOP:
		break;
	case OP_OUT8:
		decode_out8(env, *(uint8_t *)env->cur_ip);
		env->cur_ip++;
		env->regs.eip++;
		break;
	case OP_OUT:
		decode_out(env, *(uint8_t *)env->cur_ip);
		env->cur_ip += 2;
		env->regs.eip += 2;
		break;
	case OP_OUT8_dx:
		decode_out8(env, env->regs.dx);
		break;
	case OP_OUT_dx:
		decode_out(env, env->regs.dx);
		break;
	case OP_OUTS8: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = &env->regs.dx;
		decode_string_instructions_8(env, dst, 1, src, 0, outs_8);
		break;
	}
	case OP_OUTS: {
		void *src = GET_REG_POINTER(env, ds, si);
		void *dst = &env->regs.dx;
		decode_string_instructions_16(env, dst, 1, src, 0, outs_16, outs_32);
		break;
	}
	case OP_POP_rm:
		decode_pop_rm(env);
		break;
	case OP_POP_r ...(OP_POP_r + 7):
		decode_pop_r(env, *opcode);
		break;
	case OP_POP_DS:
		POP_SREG(env, ds);
		break;
	case OP_POP_ES:
		POP_SREG(env, es);
		break;
	case OP_POP_SS:
		POP_SREG(env, ss);
		break;
	case OP_POPA_POPAD:
		decode_popa_popad(env);
		break;
	case OP_POPF_POPFD:
		if (env->flags.operand_size == 0) {
			POP(env, env->regs.flags, 2);
		} else {
			POP(env, env->regs.eflags, 4);
		}
		break;
	case OP_PUSH_CS:
		PUSH_SREG(env, cs);
		break;
	case OP_PUSH_SS:
		PUSH_SREG(env, ss);
		break;
	case OP_PUSH_DS:
		PUSH_SREG(env, ds);
		break;
	case OP_PUSH_ES:
		PUSH_SREG(env, es);
		break;
	case OP_PUSH_imm8:
		PUSH(env, *(uint8_t *)env->cur_ip, 1);
		env->regs.eip++;
		break;
	case OP_PUSH_imm:
		if (env->flags.operand_size == 0) {
			PUSH(env, *(uint16_t *)env->cur_ip, 2);
			env->regs.eip += 2;
			env->cur_ip += 2;
		} else {
			PUSH(env, *(uint32_t *)env->cur_ip, 4);
			env->regs.eip += 4;
			env->cur_ip += 4;
		}
		break;
	case OP_PUSH_r ...(OP_PUSH_r + 7):
		if (env->flags.operand_size == 0) {
			uint16_t *reg = PLUS_RW_REG(env, *opcode);
			PUSH(env, *reg, 2);
		} else {
			uint32_t *reg = PLUS_RD_REG(env, *opcode);
			PUSH(env, *reg, 4);
		}
		break;
	case OP_PUSHA_PUSHAD:
		decode_pusha_pushad(env);
		break;
	case OP_PUSHF_PUSHFD:
		if (env->flags.operand_size == 0) {
			PUSH(env, env->regs.flags, 2);
		} else {
			PUSH(env, env->regs.eflags & 0x00fcffff, 4);
		}
		break;
	case OP_RET:
		exception = decode_ret_near(env, 0);
		break;
	case OP_LongRET:
		exception = decode_ret_far(env, 0);
		break;
	case OP_RET_imm16:
		exception = decode_ret_imm16(env);
		break;
	case OP_LongRET_imm16:
		exception = decode_ret_far_imm16(env);
		break;
	case OP_SAHF:
		SAHF(env);
		break;
	case OP_SCAS8: {
		void *src = GET_REG_POINTER(env, es, di);
		void *dst = &env->regs.al;
		decode_string_instructions_8(env, dst, 0, src, 1, cmps_8);
		break;
	}
	case OP_SCAS: {
		void *src = GET_REG_POINTER(env, es, di);
		void *dst = GET_REG_ADDR(env, ax);
		decode_string_instructions_16(env, dst, 0, src, 1, cmps_16, cmps_32);
		break;
	}
	case OP_STC:
		STC(env);
		break;
	case OP_STD:
		STD(env);
		break;
	case OP_STI:
		STI(env);
		io_sti();
		break;
	case OP_STOS8: {
		void *src = &env->regs.al;
		void *dst = (void *)get_phy_addr(env, env->regs.es, env->regs.di);
		decode_string_instructions_8(env, dst, 1, src, 0, movs_8);
		break;
	}
	case OP_STOS: {
		void *src = GET_REG_ADDR(env, ax);
		void *dst = (void *)get_phy_addr(env, env->regs.es, env->regs.di);
		decode_string_instructions_16(env, dst, 1, src, 0, movs_16, movs_32);
		break;
	}
	case OP_TEST_imm8:
		calc_a_imm8(env, CALC_TEST);
		break;
	case OP_TEST_imm:
		calc_a_imm(env, CALC_TEST);
		break;
	case OP_TEST_rm_r_8:
		calc_rm_r_8(env, CALC_TEST);
		break;
	case OP_TEST_rm_r:
		calc_rm_r(env, CALC_TEST);
		break;
	case (OP_XCHG_r + 1)...(OP_XCHG_r + 7): // XCHG ax, ax机器码与NOP指令相同
		exception = decode_r_rm(env, xchg_16_16, xchg_32_32);
		break;
	case OP_XCHG_8:
		exception = decode_rm8_r8(env, xchg_8_8);
		break;
	case OP_XCHG:
		exception = decode_rm_r(env, xchg_16_16, xchg_32_32);
		break;
	case OP_XLAT:
		if (env->flags.operand_size == 0) {
			env->regs.al = *(uint8_t *)get_phy_addr(
				env, env->regs.ds, env->regs.bx + env->regs.al);
		} else {
			env->regs.al = *(uint32_t *)get_phy_addr(
				env, env->regs.ds, env->regs.ebx + env->regs.al);
		}
		break;
		ALU_CASE(ADC)
		ALU_CASE(ADD)
		ALU_CASE(AND)
		ALU_CASE(XOR)
		ALU_CASE(OR)
		ALU_CASE(SBB)
		ALU_CASE(SUB)
		ALU_CASE(CMP)
	case OP_TWO_BYTES:
		exception = decode_two_bytes_opcode(env);
		break;
	case 0x80 ... 0x83:
		exception = decode_0x80_0x83(env, *opcode);
		break;
	case 0xc0:
		exception = decode_0xc0(env);
		break;
	case 0xc1:
		exception = decode_0xc1(env);
		break;
	case 0xd0:
		exception = decode_0xd0(env);
		break;
	case 0xd1:
		exception = decode_0xd1(env);
		break;
	case 0xd2:
		exception = decode_0xd2(env);
		break;
	case 0xd3:
		exception = decode_0xd3(env);
		break;
	case 0xf6:
		exception = decode_0xf6(env);
		break;
	case 0xf7:
		exception = decode_0xf7(env);
		break;
	case 0xfe:
		exception = decode_0xfe(env);
		break;
	case 0xff:
		exception = decode_0xff(env);
		break;
	default:
		env->regs.eip--;
		env->cur_ip--;
		return InvalidOpcode;
	}
	return exception;
}

BiosEmuExceptions emu_run_instruction(BiosEmuEnvironment *env) {
	BiosEmuPrefixes prefix;
	env->default_ss			= &env->regs.ds;
	env->flags.operand_size = env->flags.default_operand_size;
	env->flags.address_size = env->flags.default_address_size;
	env->flags.repeat		= 0;
	int flag				= 1;
	while (flag) {
		prefix = *(BiosEmuPrefixes *)env->cur_ip;
		env->cur_ip++;
		env->regs.eip++;
		switch ((uint8_t)prefix) {
		case PREFIX_LOCK:
			break;
		case PREFIX_REPNE_REPNZ_BND:
			env->flags.repeat	= 1;
			env->flags.rep_e_ne = 0;
			break;
		case PREFIX_REP_REPE_REPZ:
			env->flags.repeat	= 1;
			env->flags.rep_e_ne = 1;
			break;
		case PREFIX_CS_OVERRIDE_BRANCH_NOT_TAKEN:
			env->default_ss = &env->regs.cs;
			break;
		case PREFIX_SS_OVERRIDE:
			env->default_ss = &env->regs.ss;
			break;
		case PREFIX_DS_OVERRIDE_BRANCH_TAKEN:
			env->default_ss = &env->regs.ds;
			break;
		case PREFIX_ES_OVERRIDE:
			env->default_ss = &env->regs.es;
			break;
		case PREFIX_FS_OVERRIDE:
			env->default_ss = &env->regs.fs;
			break;
		case PREFIX_GS_OVERRIDE:
			env->default_ss = &env->regs.gs;
			break;
		case PREFIX_OPERAND_SIZE_OVERRIDE:
			env->flags.operand_size ^= 1;
			break;
		case PREFIX_ADDRSIZE_OVERRIDE:
			env->flags.address_size ^= 1;
			break;
		default:
			env->regs.eip--;
			env->cur_ip--;
			flag = 0;
			break;
		}
	}
	return decode_one_byte_opcode(env);
}

BiosEmuExceptions emu_run(BiosEmuEnvironment *env) {
	BiosEmuExceptions exception = NoException;

	while (exception == NoException) {
		exception = emu_run_instruction(env);
		if (exception != NoException) { break; }
	}

	return exception;
}
