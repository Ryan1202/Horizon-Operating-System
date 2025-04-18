#include "../includes/stack.h"
#include "../includes/decode.h"
#include "../includes/mod_rm.h"
#include "../includes/operations.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

void decode_pusha_pushad(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			uint16_t temp_sp = env->regs.sp;
			PUSH16(env, env->regs.ax, 2);
			PUSH16(env, env->regs.cx, 2);
			PUSH16(env, env->regs.dx, 2);
			PUSH16(env, env->regs.bx, 2);
			PUSH16(env, temp_sp, 2);
			PUSH16(env, env->regs.bp, 2);
			PUSH16(env, env->regs.si, 2);
			PUSH16(env, env->regs.di, 2);
		} else {
			uint16_t temp_sp = env->regs.sp;
			PUSH32(env, env->regs.eax, 2);
			PUSH32(env, env->regs.ecx, 2);
			PUSH32(env, env->regs.edx, 2);
			PUSH32(env, env->regs.ebx, 2);
			PUSH32(env, temp_sp, 2);
			PUSH32(env, env->regs.ebp, 2);
			PUSH32(env, env->regs.esi, 2);
			PUSH32(env, env->regs.edi, 2);
		}
	} else {
		if (env->flags.stack_size == 0) {
			uint32_t temp_esp = env->regs.esp;
			PUSH16(env, env->regs.eax, 4);
			PUSH16(env, env->regs.ecx, 4);
			PUSH16(env, env->regs.edx, 4);
			PUSH16(env, env->regs.ebx, 4);
			PUSH16(env, temp_esp, 4);
			PUSH16(env, env->regs.ebp, 4);
			PUSH16(env, env->regs.esi, 4);
			PUSH16(env, env->regs.edi, 4);
		} else {
			uint32_t temp_esp = env->regs.esp;
			PUSH32(env, env->regs.eax, 4);
			PUSH32(env, env->regs.ecx, 4);
			PUSH32(env, env->regs.edx, 4);
			PUSH32(env, env->regs.ebx, 4);
			PUSH32(env, temp_esp, 4);
			PUSH32(env, env->regs.ebp, 4);
			PUSH32(env, env->regs.esi, 4);
			PUSH32(env, env->regs.edi, 4);
		}
	}
	return;
}

void decode_pop_rm(BiosEmuEnvironment *env) {
	uint8_t modrm = *(uint8_t *)env->cur_ip++;
	env->regs.eip++;

	if (env->flags.operand_size == 0) {
		uint16_t *addr = RM_ADDR16(env, modrm);
		POP(env, *addr, 2);
	} else {
		uint32_t *addr = RM_ADDR32(env, modrm);
		POP(env, *addr, 4);
	}
	return;
}

void decode_pop_r(BiosEmuEnvironment *env, uint8_t opcode) {
	if (env->flags.operand_size == 0) {
		uint16_t *dst = PLUS_RW_REG(env, opcode);
		POP(env, *dst, 2);
	} else {
		uint32_t *dst = PLUS_RD_REG(env, opcode);
		POP(env, *dst, 4);
	}
	return;
}

void decode_popa_popad(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			POP16(env, env->regs.di, 2);
			POP16(env, env->regs.si, 2);
			POP16(env, env->regs.bp, 2);
			env->regs.sp += 2;
			POP16(env, env->regs.bx, 2);
			POP16(env, env->regs.dx, 2);
			POP16(env, env->regs.cx, 2);
			POP16(env, env->regs.ax, 2);
		} else {
			POP32(env, env->regs.di, 2);
			POP32(env, env->regs.si, 2);
			POP32(env, env->regs.bp, 2);
			env->regs.esp += 2;
			POP32(env, env->regs.bx, 2);
			POP32(env, env->regs.dx, 2);
			POP32(env, env->regs.cx, 2);
			POP32(env, env->regs.ax, 2);
		}
	} else {
		if (env->flags.stack_size == 0) {
			POP16(env, env->regs.edi, 4);
			POP16(env, env->regs.esi, 4);
			POP16(env, env->regs.ebp, 4);
			env->regs.sp += 4;
			POP16(env, env->regs.ebx, 4);
			POP16(env, env->regs.edx, 4);
			POP16(env, env->regs.ecx, 4);
			POP16(env, env->regs.eax, 4);
		} else {
			POP32(env, env->regs.edi, 4);
			POP32(env, env->regs.esi, 4);
			POP32(env, env->regs.ebp, 4);
			env->regs.esp += 4;
			POP32(env, env->regs.ebx, 4);
			POP32(env, env->regs.edx, 4);
			POP32(env, env->regs.ecx, 4);
			POP32(env, env->regs.eax, 4);
		}
	}
	return;
}