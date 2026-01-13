#ifndef _BIOS_EMU_ENVIRONMENT_H
#define _BIOS_EMU_ENVIRONMENT_H

#include <stdint.h>

#define DEF_GENERIC_REG(name) \
	union {                   \
		struct {              \
			uint8_t name##l;  \
			uint8_t name##h;  \
		};                    \
		uint16_t name##x;     \
		uint32_t e##name##x;  \
	};
#define DEF_OTHER_REG(name) \
	union {                 \
		uint16_t name;      \
		uint32_t e##name;   \
	};

typedef struct BiosEmuEnvironment {
	// The CPU state
	struct {
		DEF_GENERIC_REG(a);
		DEF_GENERIC_REG(b);
		DEF_GENERIC_REG(c);
		DEF_GENERIC_REG(d);
		DEF_OTHER_REG(si);
		DEF_OTHER_REG(di);
		DEF_OTHER_REG(bp);
		DEF_OTHER_REG(sp);
		DEF_OTHER_REG(flags);
		DEF_OTHER_REG(ip);
		uint32_t cs, ds, es, fs, gs, ss;
	} regs;

	void	*cur_ip;
	uint32_t stack_bottom;
	struct {
		uint16_t offset;
		uint16_t segment;
	} *ivt;
	struct {
		uint8_t repeat				 : 1;
		uint8_t rep_e_ne			 : 1; // 0:repne/repnz, 1:repe/repz
		uint8_t stack_size			 : 1; // 0:16bit, 1:32bit
		uint8_t operand_size		 : 1; // 0:16bit, 1:32bit
		uint8_t default_operand_size : 1;
		uint8_t address_size		 : 1; // 0:16bit, 1:32bit
		uint8_t default_address_size : 1;
	} flags;
	uint32_t *default_ss;

	// 寄存器地址查找表
	// 0:al, 1:cl, 2:dl, 3:bl, 4:ah, 5:ch, 6:dh, 7:bh
	uint8_t	 *reg_lut_r8[8];
	// 0:ax, 1:cx, 2:dx, 3:bx, 4:sp, 5:bp, 6:si, 7:di
	uint16_t *reg_lut_r16[8];
	// 0:eax, 1:ecx, 2:edx, 3:ebx, 4:esp, 5:ebp, 6:esi, 7:edi
	uint32_t *reg_lut_r32[8];

	enum {
		IntDone,
	} stop_condition;
	size_t int_entry_stack; // 进入中断时的栈指针
} BiosEmuEnvironment;

#endif