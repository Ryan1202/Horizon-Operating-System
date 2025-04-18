#ifndef _BIOS_EMU_EXCEPTIONS
#define _BIOS_EMU_EXCEPTIONS

typedef enum BiosEmuExceptions {
	// x86支持的异常，下划线开头的异常是实模式不支持的
	DivideError,
	DebugException,
	NMIInterrupt,
	Breakpoint,
	Overflow,
	BOUNDRangeExceeded,
	InvalidOpcode,
	DeviceNotAvailable,
	DoubleFault,
	_Reserved1,
	_InvalidTSS,
	_SegmentNotPresent,
	StackFault,
	GeneralProtection,
	_PageFault,
	_Reserved2,
	FloatPointError,
	_AlignmentCheck,
	MachineCheck,

	// 下面是自定义的
	NoException,
	EventInterruptDone,
} BiosEmuExceptions;

#endif