#ifndef _BIOS_EMU_CONDITIONS_H
#define _BIOS_EMU_CONDITIONS_H

#include <bios_emu/environment.h>

extern int (*condition_table[16])(BiosEmuEnvironment *env);

#endif