#include "includes/stack.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bits.h>
#include <kernel/func.h>

BiosEmuEnvironment bios_emu_env;

#define INIT_LUT(env, arr, reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7) \
	arr[0] = &env->regs.reg0;                                              \
	arr[1] = &env->regs.reg1;                                              \
	arr[2] = &env->regs.reg2;                                              \
	arr[3] = &env->regs.reg3;                                              \
	arr[4] = &env->regs.reg4;                                              \
	arr[5] = &env->regs.reg5;                                              \
	arr[6] = &env->regs.reg6;                                              \
	arr[7] = &env->regs.reg7;

#define INIT_LUT_R8(env) \
	INIT_LUT(env, env->reg_lut_r8, al, cl, dl, bl, ah, ch, dh, bh);

#define INIT_LUT_R16(env) \
	INIT_LUT(env, env->reg_lut_r16, ax, cx, dx, bx, sp, bp, si, di);

#define INIT_LUT_R32(env) \
	INIT_LUT(env, env->reg_lut_r32, eax, ecx, edx, ebx, esp, ebp, esi, edi);

void bios_emu_init(void) {
	BiosEmuEnvironment *env = &bios_emu_env;
	env->regs.cs			= 0x0000;
	env->regs.ds			= 0x0000;
	env->regs.es			= 0x0000;
	env->regs.fs			= 0x0000;
	env->regs.gs			= 0x0000;
	env->regs.eip			= 0x0000;
	env->regs.eflags		= io_load_eflags();

	env->regs.eax = 0;
	env->regs.ebx = 0;
	env->regs.ecx = 0;
	env->regs.edx = 0;
	env->regs.esi = 0;
	env->regs.edi = 0;
	env->regs.ebp = 0;
	env->regs.esp = 0x7c00;

	env->default_ss	  = &env->regs.ds;
	env->ivt		  = (void *)0x0000;
	env->cur_ip		  = 0;
	env->stack_bottom = STACK_POINTER16(env) - 0x2000;

	env->flags.stack_size			= 0;
	env->flags.operand_size			= 0;
	env->flags.default_operand_size = 0;
	env->flags.address_size			= 0;
	env->flags.default_address_size = 0;

	INIT_LUT_R8(env);
	INIT_LUT_R16(env);
	INIT_LUT_R32(env);
}