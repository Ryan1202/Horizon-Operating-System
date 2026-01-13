#include "../includes/flags.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <bits.h>
#include <stdint.h>

BiosEmuExceptions bt_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit) {
	uint16_t mask = 1 << (*bit & 0x0f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	return NoException;
}

BiosEmuExceptions bt_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit) {
	uint32_t mask = 1 << (*bit & 0x1f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	return NoException;
}

BiosEmuExceptions btc_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit) {
	uint16_t mask = 1 << (*bit & 0x0f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr ^= mask;
	return NoException;
}

BiosEmuExceptions btc_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit) {
	uint32_t mask = 1 << (*bit & 0x1f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr ^= mask;
	return NoException;
}

BiosEmuExceptions btr_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit) {
	uint16_t mask = 1 << (*bit & 0x0f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr &= ~mask;
	return NoException;
}

BiosEmuExceptions btr_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit) {
	uint32_t mask = 1 << (*bit & 0x1f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr &= ~mask;
	return NoException;
}

BiosEmuExceptions bts_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit) {
	uint16_t mask = 1 << (*bit & 0x0f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr |= mask;
	return NoException;
}

BiosEmuExceptions bts_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit) {
	uint32_t mask = 1 << (*bit & 0x1f);
	env->regs.flags &= ~BIT(CarryFlagBit);
	if (*addr & mask) { env->regs.flags |= BIT(CarryFlagBit); }
	*addr |= mask;
	return NoException;
}
