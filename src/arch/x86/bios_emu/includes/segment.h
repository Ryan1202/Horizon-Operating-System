#ifndef _BIOS_EMU_SEGMENT_H
#define _BIOS_EMU_SEGMENT_H

#include <bios_emu/environment.h>
#include <stdint.h>

size_t get_segment_base(BiosEmuEnvironment *env, uint32_t segment);

uint8_t fetch_data_8(BiosEmuEnvironment *env, uint32_t segment, size_t addr);
uint8_t fetch_data_16(BiosEmuEnvironment *env, uint32_t segment, size_t addr);
uint8_t fetch_data_32(BiosEmuEnvironment *env, uint32_t segment, size_t addr);

#endif