#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <stdint.h>

#define DEF_LXS(sreg)                                             \
	BiosEmuExceptions l##sreg##_16_16(                            \
		BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr) { \
		env->regs.sreg = *addr++;                                 \
		*reg		   = *addr;                                   \
		return NoException;                                       \
	}                                                             \
	BiosEmuExceptions l##sreg##_32_32(                            \
		BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr) { \
		env->regs.ds = *(uint16_t *)addr;                         \
		addr		 = (uint32_t *)((uint16_t *)addr + 1);        \
		return NoException;                                       \
	}

DEF_LXS(ds)
DEF_LXS(ss)
DEF_LXS(es)
DEF_LXS(fs)
DEF_LXS(gs)