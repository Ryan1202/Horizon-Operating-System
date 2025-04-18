#include <bios_emu/environment.h>
#include <stdint.h>

size_t get_segment_base(BiosEmuEnvironment *env, uint32_t segment) {
	return segment << 4;
}

uint8_t fetch_data_8(BiosEmuEnvironment *env, uint32_t segment, size_t addr) {
	size_t segment_base = get_segment_base(env, segment);
	return *(uint8_t *)(segment_base + addr);
}

uint8_t fetch_data_16(BiosEmuEnvironment *env, uint32_t segment, size_t addr) {
	size_t segment_base = get_segment_base(env, segment);
	return *(uint16_t *)(segment_base + addr);
}

uint8_t fetch_data_32(BiosEmuEnvironment *env, uint32_t segment, size_t addr) {
	size_t segment_base = get_segment_base(env, segment);
	return *(uint32_t *)(segment_base + addr);
}