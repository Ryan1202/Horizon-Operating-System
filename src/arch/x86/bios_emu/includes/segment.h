#ifndef _BIOS_EMU_SEGMENT_H
#define _BIOS_EMU_SEGMENT_H

#include <bios_emu/environment.h>
#include <stdint.h>

size_t get_segment_base(BiosEmuEnvironment *env, uint32_t segment);
size_t get_phy_addr(BiosEmuEnvironment *env, uint32_t segment, size_t addr);

uint8_t fetch_data_8(BiosEmuEnvironment *env, uint32_t segment, size_t addr);
uint8_t fetch_data_16(BiosEmuEnvironment *env, uint32_t segment, size_t addr);
uint8_t fetch_data_32(BiosEmuEnvironment *env, uint32_t segment, size_t addr);

#define GET_REG_POINTER(env, segment, reg)                                   \
	({                                                                       \
		void *addr;                                                          \
		if (env->flags.operand_size == 0) {                                  \
			addr =                                                           \
				(void *)get_phy_addr(env, env->regs.segment, env->regs.reg); \
		} else {                                                             \
			addr = (void *)get_phy_addr(                                     \
				env, env->regs.segment, env->regs.e##reg);                   \
		}                                                                    \
		addr;                                                                \
	})

#endif