#ifndef BITS_H
#define BITS_H

// Include necessary standard libraries
#include <stdint.h>

// Define any macros, constants, or types here
#define SWAP_WORD(n) ((((n) & 0xff) << 8) | (((n) & 0xff00) >> 8))
#define SWAP_DWORD(n) \
	(SWAP_WORD((n) & 0xffff) << 16 | SWAP_WORD(((n) & 0xffff0000) >> 16))

#ifdef ARCH_X86

#include <kernel/func.h>

#define BE2HOST_WORD(n)	 SWAP_WORD(n)
#define BE2HOST_DWORD(n) SWAP_DWORD(n)
#define LE2HOST_WORD(n)	 (n)
#define LE2HOST_DWORD(n) (n)

#define HOST2BE_WORD(n)	 SWAP_WORD(n)
#define HOST2BE_DWORD(n) SWAP_DWORD(n)
#define HOST2LE_WORD(n)	 (n)
#define HOST2LE_DWORD(n) (n)

#define BIT(n)		 (1 << n)
#define BIT_FFS_R(n) (bsf(n)) // 从低到高找到第一个非0位的位置
#define BIT_FFS_L(n) (bsr(n)) // 从高到低找到第一个非0位的位置

#define BIN_EN(n, x)	 ((n) | x)
#define BIN_DIS(n, x)	 ((n) & ~x)
#define BIN_IS_DIS(n, x) (!BIN_EN(n, x))
#define BIN_IS_EN(n, x)	 (!BIN_IS_DIS(n, x))

#endif

// Declare any functions or variables here

#endif // BITS_H