#include "../includes/flags.h"
#include "../includes/operations.h"
#include "../includes/stack.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <bits.h>
#include <stdint.h>

BiosEmuExceptions emu_interrupt(int vector) {
	BiosEmuEnvironment *env = &bios_emu_env;
	env->stop_condition		= IntDone;
	env->int_entry_stack	= STACK_POINTER16(env);

	decode_int(env, vector);

	return emu_run(env);
}

BiosEmuExceptions decode_int(BiosEmuEnvironment *env, uint8_t vector) {
	env->regs.eip++;
	if (vector > 255) return GeneralProtection;
	if (env->stack_bottom + 6 > STACK_POINTER16(env)) return StackFault;

	PUSH(env, env->regs.flags, 2);
	env->regs.flags = BIN_DIS(
		env->regs.flags, BIT(InterruptEnableFlagBit) | BIT(TrapFlagBit) |
							 BIT(AlignmentCheckFlagBit));
	PUSH(env, env->regs.cs, 2);
	PUSH(env, env->regs.ip, 2);

	env->regs.cs = env->ivt[vector].segment;
	env->regs.ip = env->ivt[vector].offset;

	env->cur_ip = (void *)((env->regs.cs << 4) + env->regs.ip);

	return NoException;
}

BiosEmuExceptions decode_iret(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			POP16(env, env->regs.eip, 2);
			POP16(env, env->regs.cs, 2);
			POP16(env, env->regs.flags, 2);
		} else {
			POP32(env, env->regs.eip, 2);
			POP32(env, env->regs.cs, 2);
			POP32(env, env->regs.flags, 2);
		}
	} else {
		if (env->flags.stack_size == 0) {
			POP16(env, env->regs.eip, 4);
			POP16(env, env->regs.cs, 4);
			POP16(env, env->regs.eflags, 4);
		} else {
			uint32_t temp_eflags;
			POP32(env, env->regs.eip, 4);
			POP32(env, env->regs.cs, 4);
			POP32(env, temp_eflags, 4);
			env->regs.eflags =
				(temp_eflags & 0x257FD5) | (env->regs.eflags & 0x1A0000);
		}
	}
	BiosEmuExceptions exception = NoException;
	// 通过栈地址检测是否完成中断功能调用
	if (env->int_entry_stack == STACK_POINTER16(env)) {
		exception = EventInterruptDone;
	}
	return exception;
}