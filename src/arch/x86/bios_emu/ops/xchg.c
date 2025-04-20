#include "../includes/flags.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

BiosEmuExceptions xchg_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2) {
	uint8_t tmp = *addr1;
	*addr1		= *addr2;
	*addr2		= tmp;

	return NoException;
}

BiosEmuExceptions xchg_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2) {
	uint16_t tmp = *addr1;
	*addr1		 = *addr2;
	*addr2		 = tmp;

	return NoException;
}

BiosEmuExceptions xchg_32_32(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2) {
	uint16_t tmp = *addr1;
	*addr1		 = *addr2;
	*addr2		 = tmp;

	return NoException;
}

BiosEmuExceptions cmpxchg_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2) {
	if (*addr1 == *addr2) {
		env->regs.al = *addr2;
		SET_FLAG(1, ZeroFlagBit);
	} else {
		env->regs.al = *addr1;
		SET_FLAG(0, ZeroFlagBit);
	}
	return NoException;
}

BiosEmuExceptions cmpxchg_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2) {
	if (*addr1 == *addr2) {
		env->regs.ax = *addr2;
		SET_FLAG(1, ZeroFlagBit);
	} else {
		env->regs.ax = *addr1;
		SET_FLAG(0, ZeroFlagBit);
	}
	return NoException;
}

BiosEmuExceptions cmpxchg_32_32(
	BiosEmuEnvironment *env, uint32_t *addr1, uint32_t *addr2) {
	if (*addr1 == *addr2) {
		env->regs.eax = *addr2;
		SET_FLAG(1, ZeroFlagBit);
	} else {
		env->regs.eax = *addr1;
		SET_FLAG(0, ZeroFlagBit);
	}
	return NoException;
}

BiosEmuExceptions xadd_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2) {
	uint8_t tmp = *addr1;
	*addr1		= *addr2 + *addr1;
	*addr2		= tmp;
	return NoException;
}

BiosEmuExceptions xadd_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2) {
	uint16_t tmp = *addr1;
	*addr1		 = *addr2 + *addr1;
	*addr2		 = tmp;
	return NoException;
}

BiosEmuExceptions xadd_32_32(
	BiosEmuEnvironment *env, uint32_t *addr1, uint32_t *addr2) {
	uint32_t tmp = *addr1;
	*addr1		 = *addr2 + *addr1;
	*addr2		 = tmp;
	return NoException;
}
