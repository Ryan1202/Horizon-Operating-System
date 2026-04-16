#include "../includes/operations.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

void decode_jcc_8(BiosEmuEnvironment *env, int condition) {
	int8_t offset = *(int8_t *)env->cur_ip++;
	env->regs.eip++;
	if (env->flags.operand_size == 0) {
		if (condition) {
			env->cur_ip += offset;
			env->regs.eip += offset;
			env->regs.eip &= 0xffff;
			env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
		}
	} else {
		if (condition) {
			env->cur_ip += offset;
			env->regs.eip += offset;
		}
	}
	return;
}

void decode_jcc(BiosEmuEnvironment *env, int condition) {
	if (env->flags.address_size == 0) {
		int16_t offset = *(int16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		if (condition) {
			env->cur_ip += offset;
			env->regs.eip += offset;
			env->regs.eip &= 0xffff;
			env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
		}
	} else {
		int32_t offset = *(int32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		if (condition) {
			env->cur_ip += offset;
			env->regs.eip += offset;
		}
	}
	return;
}

void decode_jmp8(BiosEmuEnvironment *env) {
	int8_t offset = *(int8_t *)env->cur_ip++;
	env->regs.eip++;
	env->cur_ip += offset;
	env->regs.eip += offset;
	if (env->flags.operand_size == 0) {
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
	}
	return;
}

void decode_jmp_near(BiosEmuEnvironment *env, int32_t offset) {
	if (env->flags.operand_size == 0) {
		env->cur_ip += offset;
		env->regs.eip += offset;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
	} else {
		env->cur_ip += offset;
		env->regs.eip += offset;
	}
	return;
}

void decode_jmp_far(
	BiosEmuEnvironment *env, uint16_t segment, uint32_t offset) {
	env->regs.cs = segment;
	if (env->flags.operand_size == 0) {
		env->regs.eip = offset & 0xffff;
		env->cur_ip	  = (void *)(size_t)((segment << 4) + env->regs.eip);
	} else {
		env->regs.eip = offset;
		env->cur_ip	  = (void *)(size_t)env->regs.eip;
	}
	return;
}

void decode_jmp(BiosEmuEnvironment *env) {
	if (env->flags.address_size == 0) {
		int16_t offset = *(int16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		decode_jmp_near(env, offset);
	} else {
		int32_t offset = *(int32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		decode_jmp_near(env, offset);
	}
}

void decode_long_jmp_ptr16(BiosEmuEnvironment *env) {
	uint16_t segment = *(uint16_t *)env->cur_ip;
	env->cur_ip += 2;
	env->regs.eip += 2;
	if (env->flags.operand_size == 0) {
		uint16_t offset = *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)(size_t)((env->regs.cs << 4) + env->regs.eip);
		decode_jmp_far(env, segment, offset);
	} else {
		uint32_t offset = *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		decode_jmp_far(env, segment, offset);
	}
}
