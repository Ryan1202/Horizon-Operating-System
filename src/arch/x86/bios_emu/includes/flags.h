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

#endif