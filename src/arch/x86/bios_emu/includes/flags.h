#ifndef _BIOS_EMU_FLAGS_H
#define _BIOS_EMU_FLAGS_H

#include <bits.h>

#define CarryFlagBit		   0
#define ParityFlagBit		   2
#define AuxiliaryCarryFlagBit  4
#define ZeroFlagBit			   6
#define SignFlagBit			   7
#define TrapFlagBit			   8
#define InterruptEnableFlagBit 9
#define DirectionFlagBit	   10
#define OverflowFlagBit		   11
#define IOPLFlagBit			   12
#define NestedTaskFlagBit	   14

#define ResumeFlagBit				   16
#define VirtualModeFlagBit			   17
#define AlignmentCheckFlagBit		   18
#define VirtualInterruptFlagBit		   19
#define VirtualInterruptPendingFlagBit 20
#define IDFlagBit					   21

#define SET_FLAG(x, bit) \
	env->regs.flags =    \
		(x) ? env->regs.flags | BIT(bit) : env->regs.flags & ~BIT(bit)

#define SET_CARRY_FLAG(x)	 SET_FLAG(x, CarryFlagBit)
#define SET_OVERFLOW_FLAG(x) SET_FLAG(x, OverflowFlagBit)
#define SET_SIGN_FLAG(x)	 SET_FLAG(x, SignFlagBit)

#define SIGN_FLAG8(x)  SET_SIGN_FLAG(x >> 7)
#define SIGN_FLAG16(x) SET_SIGN_FLAG(x >> 15)
#define SIGN_FLAG32(x) SET_SIGN_FLAG(x >> 31)

#endif