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
