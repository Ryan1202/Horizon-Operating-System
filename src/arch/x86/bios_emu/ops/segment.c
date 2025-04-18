#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

BiosEmuExceptions lds_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr) {
	env->regs.ds = *addr++;
	*reg		 = *addr;

	return NoException;
}

BiosEmuExceptions lds_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr) {
	env->regs.ds = *(uint16_t *)addr;
	addr		 = (uint32_t *)((uint16_t *)addr + 1);
	*reg		 = *addr;

	return NoException;
}

BiosEmuExceptions les_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr) {
	env->regs.es = *addr++;
	*reg		 = *addr;

	return NoException;
}

BiosEmuExceptions les_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr) {
	env->regs.es = *(uint16_t *)addr;
	addr		 = (uint32_t *)((uint16_t *)addr + 1);
	*reg		 = *addr;

	return NoException;
}