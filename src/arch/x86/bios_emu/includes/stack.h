#ifndef _BIOS_EMU_STACK_H
#define _BIOS_EMU_STACK_H

#define STACK_POINTER16(env) ((size_t)((env->regs.ss << 4) + env->regs.sp))
#define STACK_POINTER32(env) ((size_t)env->regs.esp)

#define PUSH16(env, data, size)                                      \
	{                                                                \
		env->regs.sp -= size;                                        \
		if (size == 1) { *(uint8_t *)STACK_POINTER16(env) = data; }  \
		if (size == 2) { *(uint16_t *)STACK_POINTER16(env) = data; } \
		if (size == 4) { *(uint32_t *)STACK_POINTER16(env) = data; } \
	}
#define PUSH32(env, data, size)                                      \
	{                                                                \
		env->regs.esp -= size;                                       \
		if (size == 1) { *(uint8_t *)STACK_POINTER32(env) = data; }  \
		if (size == 2) { *(uint16_t *)STACK_POINTER32(env) = data; } \
		if (size == 4) { *(uint32_t *)STACK_POINTER32(env) = data; } \
	}

#define POP16(env, data, size)                          \
	{                                                   \
		if (size == 1) {                                \
			(data) = *(uint8_t *)STACK_POINTER16(env);  \
		} else if (size == 2) {                         \
			(data) = *(uint16_t *)STACK_POINTER16(env); \
		} else if (size == 4) {                         \
			(data) = *(uint32_t *)STACK_POINTER16(env); \
		}                                               \
		env->regs.sp += size;                           \
	}
#define POP32(env, data, size)                          \
	{                                                   \
		if (size == 1) {                                \
			(data) = *(uint8_t *)STACK_POINTER32(env);  \
		} else if (size == 2) {                         \
			(data) = *(uint16_t *)STACK_POINTER32(env); \
		} else if (size == 4) {                         \
			(data) = *(uint32_t *)STACK_POINTER32(env); \
		}                                               \
		env->regs.esp += size;                          \
	}

#define PUSH(env, data, size)         \
	if (env->flags.stack_size == 0) { \
		PUSH16(env, data, size);      \
	} else {                          \
		PUSH32(env, data, size);      \
	}

#define POP(env, data, size)          \
	if (env->flags.stack_size == 0) { \
		POP16(env, data, size);       \
	} else {                          \
		POP32(env, data, size);       \
	}

#define PUSH_SREG(env, sreg)            \
	if (env->flags.operand_size == 0) { \
		PUSH(env, env->regs.sreg, 2);   \
	} else {                            \
		PUSH(env, env->regs.sreg, 4);   \
	}

#define POP_SREG(env, sreg)             \
	if (env->flags.operand_size == 0) { \
		POP(env, env->regs.sreg, 2);    \
	} else {                            \
		POP(env, env->regs.sreg, 4);    \
	}

#endif