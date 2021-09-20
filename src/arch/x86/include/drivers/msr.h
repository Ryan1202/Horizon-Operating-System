#ifndef MSR_H
#define MSR_H

#include <stdint.h>

#define IA32_P5_MC_ADDR					0x00
#define IA32_P5_MC_TYPE					0x01
#define IA32_MONITOR_FILTER_SIZE		0x06
#define IA32_TIME_STAMP_COUNTER			0x10
#define IA32_PLATFORM_ID				0x17
#define IA32_APIC_BASE					0x1b
#define IA32_FEATURE_CONTROL			0x3a
#define IA32_TSC_ADJUST					0x3b
#define IA32_BIOS_UPDT_TRIG				0x79
#define IA32_BIOS_SIGN_ID				0x8b
#define IA32_SGXLEPUBKEYHASH0			0x8c
#define IA32_SGXLEPUBKEYHASH1			0x8d
#define IA32_SGXLEPUBKEYHASH2			0x8e
#define IA32_SGXLEPUBKEYHASH3			0x8f
#define IA32_SMM_MONITOR_CTL			0x9b
#define IA32_SMBASE						0x9e
#define IA32_PMC0						0xc1
#define IA32_PMC1						0xc2
#define IA32_PMC2						0xc3
#define IA32_PMC3						0xc4
#define IA32_PMC4						0xc5
#define IA32_PMC5						0xc6
#define IA32_PMC6						0xc7
#define IA32_PMC7						0xc8
#define IA32_MPERF						0xe7
#define IA32_APERF						0xe8
#define IA32_MTRRCAP					0xfe
#define IA32_SYSENTER_CS				0x174
#define IA32_SYSENTER_ESP				0x175
#define IA32_SYSENTER_EIP				0x176
#define IA32_MCG_CAP					0x179
#define IA32_MCG_STATUS					0x17a
#define IA32_MCG_CTL					0x178
#define IA32_PERFEVTSEL0				0x186
#define IA32_PERFEVTSEL1				0x187
#define IA32_PERFEVTSEL2				0x188
#define IA32_PERFEVTSEL3				0x189
#define IA32_PERF_STATUS				0x198
#define IA32_PERF_CTL					0x199
#define IA32_CLOCK_MODULATION			0x19a
#define IA32_THERM_INTERRUPT			0x19b
#define IA32_THERM_STATUS				0x19c
#define IA32_MISC_ENABLE				0x1a0
#define IA32_ENERGY_PERF_BIAS			0x1b0
#define IA32_PACKAGE_THERM_STATUS		0x1b1
#define IA32_PACKAGE_THERM_INTERRUPT	0x1b2
#define IA32_DEBUGCTL					0x1d9
#define IA32_SMRR_PHYSBASE				0x1f2
#define IA32_SMRR_PHYSMASK				0x1f3
#define IA32_PLATFORM_DCA_CAP			0x1f8
#define IA32_CPU_DCA_CAP				0x1f9
#define IA32_DCA_0_CAP					0x1fa
#define IA32_MTRR_PHYSBASE0				0x200
#define IA32_MTRR_PHYSMASK0				0x201
#define IA32_MTRR_PHYSBASE1				0x202
#define IA32_MTRR_PHYSMASK1				0x203
#define IA32_MTRR_PHYSBASE2				0x204
#define IA32_MTRR_PHYSMASK2				0x205
#define IA32_MTRR_PHYSBASE3				0x206
#define IA32_MTRR_PHYSMASK3				0x207
#define IA32_MTRR_PHYSBASE4				0x208
#define IA32_MTRR_PHYSMASK4				0x209
#define IA32_MTRR_PHYSBASE5				0x20a
#define IA32_MTRR_PHYSMASK5				0x20b
#define IA32_MTRR_PHYSBASE6				0x20c
#define IA32_MTRR_PHYSMASK6				0x20d
#define IA32_MTRR_PHYSBASE7				0x20e
#define IA32_MTRR_PHYSMASK7				0x20f
#define IA32_MTRR_PHYSBASE8				0x210
#define IA32_MTRR_PHYSMASK8				0x211
#define IA32_MTRR_PHYSBASE9				0x212
#define IA32_MTRR_PHYSMASK9				0x213
#define IA32_MTRR_FIX64K_00000			0x250
#define IA32_MTRR_FIX16K_80000			0x258
#define IA32_MTRR_FIX16K_A0000			0x259
#define IA32_MTRR_FIX4K_C0000			0x268
#define IA32_MTRR_FIX4K_C8000			0x269
#define IA32_MTRR_FIX4K_D0000			0x26a
#define IA32_MTRR_FIX4K_D8000			0x26b
#define IA32_MTRR_FIX4K_E0000			0x26c
#define IA32_MTRR_FIX4K_E8000			0x26d
#define IA32_MTRR_FIX4K_F0000			0x26e
#define IA32_MTRR_FIX4K_F8000			0x26f
#define IA32_PAT						0x277
#define IA32_MC0_CTL2					0x280
#define IA32_MC1_CTL2					0x281
#define IA32_MC2_CTL2					0x282
#define IA32_MC3_CTL2					0x283
#define IA32_MC4_CTL2					0x284
#define IA32_MC5_CTL2					0x285
#define IA32_MC6_CTL2					0x286
#define IA32_MC7_CTL2					0x287
#define IA32_MC8_CTL2					0x288
#define IA32_MC9_CTL2					0x289
#define IA32_MC10_CTL2					0x28a
#define IA32_MC11_CTL2					0x28b
#define IA32_MC12_CTL2					0x28c
#define IA32_MC13_CTL2					0x28d
#define IA32_MC14_CTL2					0x28e
#define IA32_MC15_CTL2					0x28f
#define IA32_MC16_CTL2					0x290
#define IA32_MC17_CTL2					0x291
#define IA32_MC18_CTL2					0x292
#define IA32_MC19_CTL2					0x293
#define IA32_MC20_CTL2					0x294
#define IA32_MC21_CTL2					0x295
#define IA32_MC22_CTL2					0x296
#define IA32_MC23_CTL2					0x297
#define IA32_MC24_CTL2					0x298
#define IA32_MC25_CTL2					0x299
#define IA32_MC26_CTL2					0x29a
#define IA32_MC27_CTL2					0x29b
#define IA32_MC28_CTL2					0x29c
#define IA32_MC29_CTL2					0x29d
#define IA32_MC30_CTL2					0x29e
#define IA32_MC31_CTL2					0x29f
#define IA32_MTRR_DEF_TYPE				0x2ff
#define IA32_FIXED_CTR0					0x309
#define IA32_FIXED_CTR1					0x30a
#define IA32_FIXED_CTR2					0x30b
#define IA32_PERF_CAPABILITIES			0x345
#define IA32_FIXED_CTR_CTRL				0x38d
#define IA32_PERF_GLOBAL_STATUS			0x38e
#define IA32_PERF_GLOBAL_CTRL			0x38f
#define IA32_PERF_GLOBAL_OVF_CTRL		0x390
#define IA32_PERF_GLOBAL_STATUS_RESET	0x390
#define IA32_PERF_GLOBAL_STATUS_SET		0x391
#define IA32_PERF_GLOBAL_INUSE			0x392
#define IA32_PEBS_ENABLE				0x3f1
#define IA32_MC0_CTL					0x400
#define IA32_MC0_STATUS					0x401
#define IA32_MC0_ADDR					0x402
#define IA32_MC0_MISC					0x403
#define IA32_MC1_CTL					0x404
#define IA32_MC1_STATUS					0x405
#define IA32_MC1_ADDR					0x406
#define IA32_MC1_MISC					0x407
#define IA32_MC2_CTL					0x408
#define IA32_MC2_STATUS					0x409
#define IA32_MC2_ADDR					0x40a
#define IA32_MC2_MISC					0x40b
#define IA32_MC3_CTL					0x40c
#define IA32_MC3_STATUS					0x40d
#define IA32_MC3_ADDR					0x40e
#define IA32_MC3_MISC					0x40f
#define IA32_MC4_CTL					0x410
#define IA32_MC4_STATUS					0x411
#define IA32_MC4_ADDR					0x412
#define IA32_MC4_MISC					0x413
#define IA32_MC5_CTL					0x414
#define IA32_MC5_STATUS					0x415
#define IA32_MC5_ADDR					0x416
#define IA32_MC5_MISC					0x417
#define IA32_MC6_CTL					0x418
#define IA32_MC6_STATUS					0x419
#define IA32_MC6_ADDR					0x41a
#define IA32_MC6_MISC					0x41b
#define IA32_MC7_CTL					0x41c
#define IA32_MC7_STATUS					0x41d
#define IA32_MC7_ADDR					0x41e
#define IA32_MC7_MISC					0x41f
#define IA32_MC8_CTL					0x420
#define IA32_MC8_STATUS					0x421
#define IA32_MC8_ADDR					0x422
#define IA32_MC8_MISC					0x423
#define IA32_MC9_CTL					0x424
#define IA32_MC9_STATUS					0x425
#define IA32_MC9_ADDR					0x426
#define IA32_MC9_MISC					0x427
#define IA32_MC10_CTL					0x428
#define IA32_MC10_STATUS				0x429
#define IA32_MC10_ADDR					0x42a
#define IA32_MC10_MISC					0x42b
#define IA32_MC11_CTL					0x42c
#define IA32_MC11_STATUS				0x42d
#define IA32_MC11_ADDR					0x42e
#define IA32_MC11_MISC					0x42f
#define IA32_MC12_CTL					0x430
#define IA32_MC12_STATUS				0x431
#define IA32_MC12_ADDR					0x432
#define IA32_MC12_MISC					0x433
#define IA32_MC13_CTL					0x434
#define IA32_MC13_STATUS				0x435
#define IA32_MC13_ADDR					0x436
#define IA32_MC13_MISC					0x437
#define IA32_MC14_CTL					0x438
#define IA32_MC14_STATUS				0x439
#define IA32_MC14_ADDR					0x43a
#define IA32_MC14_MISC					0x43b
#define IA32_MC15_CTL					0x43c
#define IA32_MC15_STATUS				0x43d
#define IA32_MC15_ADDR					0x43e
#define IA32_MC15_MISC					0x43f
#define IA32_MC16_CTL					0x440
#define IA32_MC16_STATUS				0x441
#define IA32_MC16_ADDR					0x442
#define IA32_MC16_MISC					0x443
#define IA32_MC17_CTL					0x444
#define IA32_MC17_STATUS				0x445
#define IA32_MC17_ADDR					0x446
#define IA32_MC17_MISC					0x447
#define IA32_MC18_CTL					0x448
#define IA32_MC18_STATUS				0x449
#define IA32_MC18_ADDR					0x44a
#define IA32_MC18_MISC					0x44b
#define IA32_MC19_CTL					0x44c
#define IA32_MC19_STATUS				0x44d
#define IA32_MC19_ADDR					0x44e
#define IA32_MC19_MISC					0x44f
#define IA32_MC20_CTL					0x450
#define IA32_MC20_STATUS				0x451
#define IA32_MC20_ADDR					0x452
#define IA32_MC20_MISC					0x453
#define IA32_MC21_CTL					0x454
#define IA32_MC21_STATUS				0x455
#define IA32_MC21_ADDR					0x456
#define IA32_MC21_MISC					0x457
#define IA32_MC22_CTL					0x458
#define IA32_MC22_STATUS				0x459
#define IA32_MC22_ADDR					0x45a
#define IA32_MC22_MISC					0x45b
#define IA32_MC23_CTL					0x45c
#define IA32_MC23_STATUS				0x45d
#define IA32_MC23_ADDR					0x45e
#define IA32_MC23_MISC					0x45f
#define IA32_MC24_CTL					0x460
#define IA32_MC24_STATUS				0x461
#define IA32_MC24_ADDR					0x462
#define IA32_MC24_MISC					0x463
#define IA32_MC25_CTL					0x464
#define IA32_MC25_STATUS				0x465
#define IA32_MC25_ADDR					0x466
#define IA32_MC25_MISC					0x467
#define IA32_MC26_CTL					0x468
#define IA32_MC26_STATUS				0x469
#define IA32_MC26_ADDR					0x46a
#define IA32_MC26_MISC					0x46b
#define IA32_MC27_CTL					0x46c
#define IA32_MC27_STATUS				0x46d
#define IA32_MC27_ADDR					0x46e
#define IA32_MC27_MISC					0x46f
#define IA32_MC28_CTL					0x470
#define IA32_MC28_STATUS				0x471
#define IA32_MC28_ADDR					0x472
#define IA32_MC28_MISC					0x473
#define IA32_VMX_BASIC					0x480
#define IA32_VMX_PINBASED_CTLS			0x481
#define IA32_VMX_PROCBASED_CTLS			0x482
#define IA32_VMX_EXIT_CTLS				0x483
#define IA32_VMX_ENTRY_CTLS				0x484
#define IA32_VMX_MISC					0x485
#define IA32_VMX_CR0_FIXED0				0x486
#define IA32_VMX_CR0_FIXED1				0x487
#define IA32_VMX_CR4_FIXED0				0x488
#define IA32_VMX_CR4_FIXED1				0x489
#define IA32_VMX_VMCS_ENUM				0x48a
#define IA32_VMX_PROCBASED_CTLS2		0x48b
#define IA32_VMX_EPT_VPID_CAP			0x48c
#define IA32_VMX_TRUE_PINBASED_CTLS		0x48d
#define IA32_VMX_TRUE_PROCBASED_CTLS	0x48e
#define IA32_VMX_TRUE_EXIT_CTLS			0x48f
#define IA32_VMX_TRUE_ENTRY_CTLS		0x490
#define IA32_VMX_VMFUNC					0x491
#define IA32_A_PMC0						0x4c1
#define IA32_A_PMC1						0x4c2
#define IA32_A_PMC2						0x4c3
#define IA32_A_PMC3						0x4c4
#define IA32_A_PMC4						0x4c5
#define IA32_A_PMC5						0x4c6
#define IA32_A_PMC6						0x4c7
#define IA32_A_PMC7						0x4c8
#define IA32_MCG_EXT_CTL				0x4d0
#define IA32_SGX_SVN_STATUS				0x500
#define IA32_RTIT_OUTPUT_BASE			0x560
#define IA32_RTIT_OUTPUT_MASK_PTRS		0x561
#define IA32_RTIT_CTL					0x570
#define IA32_RTIT_STATUS				0x571
#define IA32_RTIT_CR3_MATCH				0x572
#define IA32_RTIT_ADDR0_A				0x580
#define IA32_RTIT_ADDR0_B				0x581
#define IA32_RTIT_ADDR1_A				0x582
#define IA32_RTIT_ADDR1_B				0x583
#define IA32_RTIT_ADDR2_A				0x584
#define IA32_RTIT_ADDR2_B				0x585
#define IA32_RTIT_ADDR3_A				0x586
#define IA32_RTIT_ADDR3_B				0x587
#define IA32_DS_AREA					0x600
#define IA32_TSC_DEADLINE				0x6e0
#define IA32_PM_ENABLE					0x770
#define IA32_HWP_CAPABILITIES			0x771
#define IA32_HWP_REQUEST_PKG			0x772
#define IA32_HWP_INTERRUPT				0x773
#define IA32_HWP_REQUEST				0x774
#define IA32_HWP_STATUS					0x777
#define IA32_X2APIC_APICID				0x802
#define IA32_X2APIC_VERSION				0x803
#define IA32_X2APIC_TPR					0x808
#define IA32_X2APIC_PPR					0x80a
#define IA32_X2APIC_EOI					0x80b
#define IA32_X2APIC_LDR					0x80d
#define IA32_X2APIC_SIVR				0x80f
#define IA32_X2APIC_ISR0				0x810
#define IA32_X2APIC_ISR1				0x811
#define IA32_X2APIC_ISR2				0x812
#define IA32_X2APIC_ISR3				0x813
#define IA32_X2APIC_ISR4				0x814
#define IA32_X2APIC_ISR5				0x815
#define IA32_X2APIC_ISR6				0x816
#define IA32_X2APIC_ISR7				0x817
#define IA32_X2APIC_TMR0				0x818
#define IA32_X2APIC_TMR1				0x819
#define IA32_X2APIC_TMR2				0x81a
#define IA32_X2APIC_TMR3				0x81b
#define IA32_X2APIC_TMR4				0x81c
#define IA32_X2APIC_TMR5				0x81d
#define IA32_X2APIC_TMR6				0x81e
#define IA32_X2APIC_TMR7				0x81f
#define IA32_X2APIC_IRR0				0x820
#define IA32_X2APIC_IRR1				0x821
#define IA32_X2APIC_IRR2				0x822
#define IA32_X2APIC_IRR3				0x823
#define IA32_X2APIC_IRR4				0x824
#define IA32_X2APIC_IRR5				0x825
#define IA32_X2APIC_IRR6				0x826
#define IA32_X2APIC_IRR7				0x827
#define IA32_X2APIC_ESR					0x828
#define IA32_X2APIC_LVT_CMCI			0x82f
#define IA32_X2APIC_ICR					0x830
#define IA32_X2APIC_LVT_TIMER			0x832
#define IA32_X2APIC_LVT_THERMAL			0x833
#define IA32_X2APIC_LVT_PMI				0x834
#define IA32_X2APIC_LVT_LINT0			0x835
#define IA32_X2APIC_LVT_LINT1			0x836
#define IA32_X2APIC_LVT_ERROR			0x837
#define IA32_X2APIC_INIT_COUNT			0x838
#define IA32_X2APIC_CUR_COUNT			0x839
#define IA32_X2APIC_DIV_CONF			0x83e
#define IA32_X2APIC_SELF_IPI			0x83f
#define IA32_DEBUG_INTERFACE			0xc80
#define IA32_L3_QOS_CFG					0xc81
#define IA32_QM_EVTSEL					0xc8d
#define IA32_QM_CTR						0xc8e
#define IA32_PQR_ASSOC					0xc8f
#define IA32_L3_MASK(n)					0xc90+n
#define IA32_L2_MASK(n)					0xd10+n
#define IA32_BNDCFGS					0xd90
#define IA32_XSS						0xda0
#define IA32_PKG_HDC_CTL				0xdb0
#define IA32_PM_CTL1					0xdb1
#define IA32_THREAD_STALL				0xdb2
#define IA32_EFER						0xc0000080
#define IA32_STAR						0xc0000081
#define IA32_LSTAR						0xc0000082
#define IA32_FMASK						0xc0000084
#define IA32_FS_BASE					0xc0000100
#define IA32_GS_BASE					0xc0000101
#define IA32_KERNEL_GS_BASE				0xc0000102
#define IA32_TSC_AUX					0xc0000103

#define MSR_TEMPERATURE_TARGET			0x1a2

char cpu_HasMSR(void);
void cpu_RDMSR(uint32_t msr, uint32_t *lo, uint32_t *hi);
void cpu_WRMSR(uint32_t msr, uint32_t lo, uint32_t hi);

#endif