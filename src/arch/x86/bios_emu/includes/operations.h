#ifndef _BIOS_EMU_OPERATIONS_H
#define _BIOS_EMU_OPERATIONS_H

#include "flags.h"
#include <bios_emu/environment.h>
#include <bios_emu/exceptions.h>
#include <bits.h>

#define CLI(env) env->regs.flags &= ~BIT(InterruptEnableFlagBit)
#define STI(env) env->regs.flags |= BIT(InterruptEnableFlagBit)
#define CLD(env) env->regs.flags &= ~BIT(DirectionFlagBit)
#define STD(env) env->regs.flags |= BIT(DirectionFlagBit)
#define CLC(env) env->regs.flags &= ~BIT(CarryFlagBit)
#define STC(env) env->regs.flags |= BIT(CarryFlagBit)
#define CMC(env) env->regs.flags ^= BIT(CarryFlagBit)

#define LAHF(env) env->regs.ah = 0b00000010 | (env->regs.flags & 0b11010101)
#define SAHF(env) env->regs.flags = 0b00000010 | (env->regs.ah & 0b00111110)

int ptr_within_code_segment_limit(BiosEmuEnvironment *env, uint32_t address);

BiosEmuExceptions decode_0x80_0x83(BiosEmuEnvironment *env, uint8_t opcode);
BiosEmuExceptions decode_0xba(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xf6(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xf7(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xfe(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xff(BiosEmuEnvironment *env);

void decode_pusha_pushad(BiosEmuEnvironment *env);

void decode_mov_rm_r8(BiosEmuEnvironment *env);
void decode_mov_rm_r(BiosEmuEnvironment *env);
void decode_mov_r_rm_8(BiosEmuEnvironment *env);
void decode_mov_r_rm(BiosEmuEnvironment *env);
void decode_mov_rm_sreg(BiosEmuEnvironment *env);
void decode_mov_sreg_rm(BiosEmuEnvironment *env);
void decode_mov_sreg_rm(BiosEmuEnvironment *env);
void decode_mov_r_moffs_8(BiosEmuEnvironment *env);
void decode_mov_r_moffs(BiosEmuEnvironment *env);
void decode_mov_moffs_r_8(BiosEmuEnvironment *env);
void decode_mov_moffs_r(BiosEmuEnvironment *env);
void decode_mov_r_imm8(BiosEmuEnvironment *env, uint8_t opcode);
void decode_mov_r_imm(BiosEmuEnvironment *env, uint8_t opcode);
void decode_mov_rm_imm8(BiosEmuEnvironment *env);
void decode_mov_rm_imm(BiosEmuEnvironment *env);

void decode_movzx_r_rm8(BiosEmuEnvironment *env);
void decode_movzx_r_rm16(BiosEmuEnvironment *env);
void decode_movsx_r_rm8(BiosEmuEnvironment *env);
void decode_movsx_r_rm16(BiosEmuEnvironment *env);

void decode_jcc_8(BiosEmuEnvironment *env, int condition);
void decode_jcc(BiosEmuEnvironment *env, int condition);
void decode_jmp8(BiosEmuEnvironment *env);
void decode_jmp_near(BiosEmuEnvironment *env, int32_t offset);
void decode_jmp_far(BiosEmuEnvironment *env, uint16_t segment, uint32_t offset);
void decode_jmp(BiosEmuEnvironment *env);
void decode_long_jmp_ptr16(BiosEmuEnvironment *env);

void decode_pop_rm(BiosEmuEnvironment *env);
void decode_pop_r(BiosEmuEnvironment *env, uint8_t opcode);
void decode_popa_popad(BiosEmuEnvironment *env);

BiosEmuExceptions decode_call_near(BiosEmuEnvironment *env, uint32_t address);
BiosEmuExceptions decode_call(BiosEmuEnvironment *env);
BiosEmuExceptions decode_call_far(
	BiosEmuEnvironment *env, uint16_t segment, uint32_t offset);
BiosEmuExceptions decode_call_ptr(BiosEmuEnvironment *env);

BiosEmuExceptions decode_int(BiosEmuEnvironment *env, uint8_t vector);
BiosEmuExceptions decode_iret(BiosEmuEnvironment *env);

BiosEmuExceptions decode_ret_near(BiosEmuEnvironment *env, int bytes);
BiosEmuExceptions decode_ret_far(BiosEmuEnvironment *env, int bytes);
BiosEmuExceptions decode_ret_imm16(BiosEmuEnvironment *env);
BiosEmuExceptions decode_ret_far_imm16(BiosEmuEnvironment *env);

void decode_in8(BiosEmuEnvironment *env, uint8_t port);
void decode_in(BiosEmuEnvironment *env, uint16_t port);
void decode_out8(BiosEmuEnvironment *env, uint8_t port);
void decode_out(BiosEmuEnvironment *env, uint16_t port);

void decode_lea(BiosEmuEnvironment *env);

void decode_setcc(BiosEmuEnvironment *env, int condition);

BiosEmuExceptions decode_0xc0(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xc1(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xd0(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xd1(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xd2(BiosEmuEnvironment *env);
BiosEmuExceptions decode_0xd3(BiosEmuEnvironment *env);

BiosEmuExceptions xchg_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2);
BiosEmuExceptions xchg_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2);
BiosEmuExceptions xchg_32_32(
	BiosEmuEnvironment *env, uint32_t *addr1, uint32_t *addr2);

BiosEmuExceptions cmpxchg_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2);
BiosEmuExceptions cmpxchg_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2);
BiosEmuExceptions cmpxchg_32_32(
	BiosEmuEnvironment *env, uint32_t *addr1, uint32_t *addr2);

BiosEmuExceptions xadd_8_8(
	BiosEmuEnvironment *env, uint8_t *addr1, uint8_t *addr2);
BiosEmuExceptions xadd_16_16(
	BiosEmuEnvironment *env, uint16_t *addr1, uint16_t *addr2);
BiosEmuExceptions xadd_32_32(
	BiosEmuEnvironment *env, uint32_t *addr1, uint32_t *addr2);

BiosEmuExceptions lds_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr);
BiosEmuExceptions lds_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr);
BiosEmuExceptions lss_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr);
BiosEmuExceptions lss_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr);
BiosEmuExceptions les_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr);
BiosEmuExceptions les_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr);
BiosEmuExceptions lfs_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr);
BiosEmuExceptions lfs_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr);
BiosEmuExceptions lgs_16_16(
	BiosEmuEnvironment *env, uint16_t *reg, uint16_t *addr);
BiosEmuExceptions lgs_32_32(
	BiosEmuEnvironment *env, uint32_t *reg, uint32_t *addr);

BiosEmuExceptions shld_16_16_8(
	BiosEmuEnvironment *env, uint16_t *dest, uint16_t reg, uint8_t count);
BiosEmuExceptions shld_32_32_8(
	BiosEmuEnvironment *env, uint32_t *dest, uint32_t reg, uint8_t count);
BiosEmuExceptions shrd_16_16_8(
	BiosEmuEnvironment *env, uint16_t *dest, uint16_t reg, uint8_t count);
BiosEmuExceptions shrd_32_32_8(
	BiosEmuEnvironment *env, uint32_t *dest, uint32_t reg, uint8_t count);

void decode_cbw_cwde(BiosEmuEnvironment *env);
void decode_cwd_cdq(BiosEmuEnvironment *env);

BiosEmuExceptions bt_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit);
BiosEmuExceptions bt_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit);
BiosEmuExceptions btc_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit);
BiosEmuExceptions btc_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit);
BiosEmuExceptions btr_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit);
BiosEmuExceptions btr_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit);
BiosEmuExceptions bts_16_16(
	BiosEmuEnvironment *env, uint16_t *addr, uint16_t *bit);
BiosEmuExceptions bts_32_32(
	BiosEmuEnvironment *env, uint32_t *addr, uint32_t *bit);

typedef void (*OpStr)(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void decode_movs(BiosEmuEnvironment *env, OpStr movs_16, OpStr movs_32);
void movs_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void movs_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void movs_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);

void ins_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void ins_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void ins_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void outs_8(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void outs_16(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);
void outs_32(
	BiosEmuEnvironment *env, void *dst, int delta_dst, void *src, int delta_src,
	int repeat_times);

BiosEmuExceptions bsf_16_16(
	BiosEmuEnvironment *env, uint16_t *dst, uint16_t *src);
BiosEmuExceptions bsf_32_32(
	BiosEmuEnvironment *env, uint32_t *dst, uint32_t *src);
BiosEmuExceptions bsr_16_16(
	BiosEmuEnvironment *env, uint16_t *dst, uint16_t *src);
BiosEmuExceptions bsr_32_32(
	BiosEmuEnvironment *env, uint32_t *dst, uint32_t *src);

BiosEmuExceptions bswap_32(BiosEmuEnvironment *env, uint32_t *addr);

BiosEmuExceptions decode_enter(BiosEmuEnvironment *env);
BiosEmuExceptions decode_leave(BiosEmuEnvironment *env);

#endif