#ifndef _BIOS_EMU_H
#define _BIOS_EMU_H

#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>

extern BiosEmuEnvironment bios_emu_env;

void			  bios_emu_init(void);
BiosEmuExceptions emu_run(BiosEmuEnvironment *env);
BiosEmuExceptions emu_interrupt(int vector);

#endif