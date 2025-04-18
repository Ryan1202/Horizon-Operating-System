#include "includes/mod_rm.h"
#include <bios_emu/environment.h>
#include <stdint.h>

size_t decode_rm_address_16(BiosEmuEnvironment *env, uint8_t modrm) {
	uint8_t mod = (modrm >> 6) & 0b11;
	uint8_t rm	= modrm & 0b111;

	uint16_t offset;
	switch (rm) {
	case RM_BX_SI: // [BX + SI]
		offset = env->regs.bx + env->regs.si;
		break;
	case RM_BX_DI: // [BX + DI]
		offset = env->regs.bx + env->regs.di;
		break;
	case RM_BP_SI: // [BP + SI]
		offset = env->regs.bp + env->regs.si;
		break;
	case RM_BP_DI: // [BP + DI]
		offset = env->regs.bp + env->regs.di;
		break;
	case RM_SI: // [SI]
		offset = env->regs.si;
		break;
	case RM_DI: // [DI]
		offset = env->regs.di;
		break;
	case RM_BP: // [BP]
		offset = env->regs.bp;
		break;
	case RM_BX: // [BX]
		offset = env->regs.bx;
		break;
	}

	switch (mod) {
	case MOD_BASE:
		if (rm == 0b110) {
			// disp16
			offset = *(uint16_t *)env->cur_ip;
			env->cur_ip += 2;
			env->regs.eip += 2;
			env->regs.eip &= 0xffff;
			env->cur_ip = (void *)((env->regs.cs << 4) + env->regs.eip);
		} else if (rm == 0b111) {
			// [BX]
			offset = env->regs.bx;
		}
		break;
	case MOD_BASE_DISP8:
		offset += *(int8_t *)env->cur_ip;
		env->cur_ip += 1;
		env->regs.eip += 1;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)((env->regs.cs << 4) + env->regs.eip);
		break;
	case MOD_BASE_DISP16:
		offset += *(uint16_t *)env->cur_ip;
		env->cur_ip += 2;
		env->regs.eip += 2;
		env->regs.eip &= 0xffff;
		env->cur_ip = (void *)((env->regs.cs << 4) + env->regs.eip);
		break;
	default:
		break;
	}

	return offset;
}

size_t decode_rm_address_32(BiosEmuEnvironment *env, uint8_t modrm) {
	uint8_t mod = (modrm >> 6) & 0b11;
	uint8_t rm	= modrm & 0b111;

	uint32_t offset = 0;
	if (mod != 0b11) {
		switch (rm) {
		case RM_EAX: // [EAX]
			offset = env->regs.eax;
			break;
		case RM_ECX: // [ECX]
			offset = env->regs.ecx;
			break;
		case RM_EDX: // [EDX]
			offset = env->regs.edx;
			break;
		case RM_EBX: // [EBX]
			offset = env->regs.ebx;
			break;
		case RM_EBP: // [EBP]
			offset = env->regs.ebp;
			break;
		case RM_ESI: // [DSI]
			offset = env->regs.esi;
			break;
		case RM_EDI: // [EDI]
			offset = env->regs.edi;
			break;
		}
	}

	switch (mod) {
	case MOD_BASE:
		if (rm == 0b101) {
			// disp32
			offset = *(uint32_t *)env->cur_ip;
			env->cur_ip += 4;
			env->regs.eip += 4;
		}
		break;
	case MOD_BASE_DISP8:
		offset += *(int8_t *)env->cur_ip;
		env->cur_ip += 1;
		env->regs.eip += 1;
		break;
	case MOD_BASE_DISP32:
		offset += *(uint32_t *)env->cur_ip;
		env->cur_ip += 4;
		env->regs.eip += 4;
		break;
	default:
		break;
	}

	return offset;
}

size_t decode_rm_address(BiosEmuEnvironment *env, uint8_t modrm) {
	if (env->flags.address_size == 0) {
		return decode_rm_address_16(env, modrm);
	} else {
		return decode_rm_address_32(env, modrm);
	}
}
