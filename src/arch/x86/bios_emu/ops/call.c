#include "../includes/operations.h"
#include "../includes/stack.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

BiosEmuExceptions decode_call_near(BiosEmuEnvironment *env, uint32_t address) {
	if (env->flags.operand_size == 0) {
		if (!ptr_within_code_segment_limit(env, address))
			return GeneralProtection;
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 2) {
				return StackFault;
			}
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 2) {
				return StackFault;
			}
		}
		PUSH(env, env->regs.ip, 2);
		env->cur_ip	  = (void *)(size_t)((env->regs.cs << 4) + address);
		env->regs.eip = address;
	} else {
		if (!ptr_within_code_segment_limit(env, address))
			return GeneralProtection;
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 4) {
				return StackFault;
			}
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 4) {
				return StackFault;
			}
		}
		PUSH(env, env->regs.eip, 4);
		env->cur_ip	  = (void *)(size_t)address;
		env->regs.eip = address;
	}
	return NoException;
}

BiosEmuExceptions decode_call(BiosEmuEnvironment *env) {
	if (env->flags.operand_size == 0) {
		uint16_t offset = *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
		return decode_call_near(env, (env->regs.eip + offset) & 0xffff);
	} else {
		uint32_t offset = *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		return decode_call_near(env, env->regs.eip + offset);
	}
}

BiosEmuExceptions decode_call_far(
	BiosEmuEnvironment *env, uint16_t segment, uint32_t offset) {
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			PUSH16(env, env->regs.cs, 2);
			PUSH16(env, env->regs.ip, 2);
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			PUSH32(env, env->regs.cs, 2);
			PUSH32(env, env->regs.ip, 2);
		}
		env->regs.cs  = segment;
		env->regs.eip = offset & 0xffff;
		env->cur_ip	  = (void *)(size_t)((segment << 4) + env->regs.eip);
	} else {
		if (offset >> 16 != 0) return GeneralProtection; // 实模式
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 6) {
				return StackFault;
			}
			PUSH16(env, env->regs.cs, 4);
			PUSH16(env, env->regs.eip, 4);
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 6) {
				return StackFault;
			}
			PUSH32(env, env->regs.cs, 4);
			PUSH32(env, env->regs.eip, 4);
		}
		env->regs.cs  = segment;
		env->regs.eip = offset;
		env->cur_ip	  = (void *)(size_t)env->regs.eip;
	}
	return NoException;
}

BiosEmuExceptions decode_call_ptr(BiosEmuEnvironment *env) {
	uint16_t segment = *(uint16_t *)env->cur_ip;
	if (env->flags.operand_size == 0) {
		uint16_t offset = *(uint16_t *)(env->cur_ip + 2);
		env->cur_ip += 4;
		env->regs.eip += 4;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
		return decode_call_far(env, segment, offset);
	} else {
		uint32_t offset = *(uint32_t *)env->cur_ip;
		env->cur_ip += 6;
		env->regs.eip += 6;
		return decode_call_far(env, segment, offset);
	}
}

BiosEmuExceptions decode_ret_near(BiosEmuEnvironment *env, int bytes) {
	uint32_t temp_eip;
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 2) {
				return StackFault;
			}
			POP16(env, temp_eip, 2);
			env->regs.sp += bytes;
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 2) {
				return StackFault;
			}
			POP32(env, temp_eip, 2);
			env->regs.esp += bytes;
		}
		temp_eip &= 0xffff;
		if (!ptr_within_code_segment_limit(env, temp_eip))
			return GeneralProtection;
		env->regs.eip = temp_eip;
		env->cur_ip	  = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
	} else {
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			POP16(env, temp_eip, 4);
			env->regs.sp += bytes;
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			POP32(env, temp_eip, 4);
			env->regs.esp += bytes;
		}
		env->regs.eip = temp_eip;
		env->cur_ip	  = (void *)(size_t)env->regs.eip;
	}
	return NoException;
}

BiosEmuExceptions decode_ret_far(BiosEmuEnvironment *env, int bytes) {
	uint16_t segment;
	uint32_t temp_eip;
	if (env->flags.operand_size == 0) {
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			POP16(env, temp_eip, 2);
			POP16(env, segment, 2);
			env->regs.sp += bytes;
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 4) {
				return StackFault;
			}
			POP32(env, temp_eip, 2);
			POP32(env, segment, 2);
			env->regs.esp += bytes;
		}
		temp_eip &= 0xffff;
		if (!ptr_within_code_segment_limit(env, temp_eip))
			return GeneralProtection;
		env->regs.cs  = segment;
		env->regs.eip = temp_eip;
		env->cur_ip	  = (void *)(size_t)((segment << 4) + env->regs.eip);
	} else {
		if (env->flags.stack_size == 0) {
			if (STACK_POINTER16(env) < env->stack_bottom + 8) {
				return StackFault;
			}
			POP16(env, env->regs.eip, 4);
			POP16(env, segment, 4);
			env->regs.sp += bytes;
		} else {
			if (STACK_POINTER32(env) < env->stack_bottom + 8) {
				return StackFault;
			}
			POP32(env, env->regs.eip, 4);
			POP32(env, segment, 4);
			env->regs.esp += bytes;
		}
		env->regs.cs = segment;
		env->cur_ip	 = (void *)(size_t)env->regs.eip;
	}
	return NoException;
}

BiosEmuExceptions decode_ret_imm16(BiosEmuEnvironment *env) {
	uint16_t offset = *(uint16_t *)env->cur_ip;
	env->cur_ip += 2;
	env->regs.eip += 2;
	return decode_ret_near(env, offset);
}

BiosEmuExceptions decode_ret_far_imm16(BiosEmuEnvironment *env) {
	uint16_t offset = *(uint16_t *)env->cur_ip;
	env->cur_ip += 2;
	env->regs.eip += 2;
	return decode_ret_far(env, offset);
}
