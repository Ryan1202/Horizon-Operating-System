#include "../includes/flags.h"
#include <bios_emu/bios_emu.h>
#include <bios_emu/environment.h>
#include <bits.h>

int condition_o(BiosEmuEnvironment *env) {
	// OF=1
	return env->regs.flags & BIT(OverflowFlagBit);
}

int condition_no(BiosEmuEnvironment *env) {
	// OF=0
	return !(env->regs.flags & BIT(OverflowFlagBit));
}

int condition_b_c_nae(BiosEmuEnvironment *env) {
	// CF=1
	return env->regs.flags & BIT(CarryFlagBit);
}

int condition_ae_nb_nc(BiosEmuEnvironment *env) {
	// CF=0
	return !(env->regs.flags & BIT(CarryFlagBit));
}

int condition_e_z(BiosEmuEnvironment *env) {
	// ZF=1
	return env->regs.flags & BIT(ZeroFlagBit);
}

int condition_ne_nz(BiosEmuEnvironment *env) {
	// ZF=0
	return !(env->regs.flags & BIT(ZeroFlagBit));
}

int condition_be_na(BiosEmuEnvironment *env) {
	// CF=1 && ZF=1
	return (env->regs.flags & BIT(CarryFlagBit)) &&
		   (env->regs.flags & BIT(ZeroFlagBit));
}

int condition_a_nbe(BiosEmuEnvironment *env) {
	// CF=0 && ZF=0
	return !(env->regs.flags & BIT(CarryFlagBit)) &&
		   !(env->regs.flags & BIT(ZeroFlagBit));
}

int condition_s(BiosEmuEnvironment *env) {
	// SF=1
	return env->regs.flags & BIT(SignFlagBit);
}

int condition_ns(BiosEmuEnvironment *env) {
	// SF=0
	return !(env->regs.flags & BIT(SignFlagBit));
}

int condition_p_pe(BiosEmuEnvironment *env) {
	// PF=1
	return env->regs.flags & BIT(ParityFlagBit);
}

int condition_np_po(BiosEmuEnvironment *env) {
	// PF=0
	return !(env->regs.flags & BIT(ParityFlagBit));
}

int condition_l_nge(BiosEmuEnvironment *env) {
	// SF!=OF
	return !!(env->regs.flags & BIT(SignFlagBit)) !=
		   !!(env->regs.flags & BIT(OverflowFlagBit));
}

int condition_nl_ge(BiosEmuEnvironment *env) {
	// SF=OF
	return !!(env->regs.flags & BIT(SignFlagBit)) ==
		   !!(env->regs.flags & BIT(OverflowFlagBit));
}

int condition_le_ng(BiosEmuEnvironment *env) {
	// ZF=1 || SF!=OF
	return (env->regs.flags & BIT(ZeroFlagBit)) ||
		   !!(env->regs.flags & BIT(SignFlagBit)) !=
			   !!(env->regs.flags & BIT(OverflowFlagBit));
}

int condition_nle_g(BiosEmuEnvironment *env) {
	// ZF=0 && SF=OF
	return !(env->regs.flags & BIT(ZeroFlagBit)) &&
		   !!(env->regs.flags & BIT(SignFlagBit)) ==
			   !!(env->regs.flags & BIT(OverflowFlagBit));
}

int (*condition_table[16])(BiosEmuEnvironment *env) = {
	condition_o,	 condition_no,	  condition_b_c_nae, condition_ae_nb_nc,
	condition_e_z,	 condition_ne_nz, condition_be_na,	 condition_a_nbe,
	condition_s,	 condition_ns,	  condition_p_pe,	 condition_np_po,
	condition_l_nge, condition_nl_ge, condition_le_ng,	 condition_nle_g};
