#ifndef APIC_H
#define APIC_H

#define APIC_ID		0x20
#define APIC_Ver	0x30
#define APIC_TPR	0x80
#define APIC_APR	0x90
#define APIC_PPR	0xa0
#define APIC_EOI	0xb0
#define APIC_RRD	0xc0
#define APIC_LDR	0xd0
#define APIC_DFR	0xe0
#define APIC_SIVR	0xf0

#define IOAPIC_ID	0x00
#define IOAPIC_VER	0x01
#define IOAPIC_ARB	0x02
#define IOAPIC_TBL	0x10

#define APIC_INT_DISABLE 0x00010000

#define APIC_ESR        0x280
#define APIC_LVT_CMCI   0x2f0
#define APIC_ICR_LOW    0x300
#define APIC_ICR_HIGH   0x310
#define	APIC_LVT_TIMER	0x320
#define	APIC_LVT_THMR	0x330
#define	APIC_LVT_PMCR	0x340
#define	APIC_LVT_LINT0	0x350
#define	APIC_LVT_LINT1	0x360
#define	APIC_LVT_ERROR	0x370

#define	APIC_TIMER_ICT	0x380
#define	APIC_TIMER_CCT	0x390
#define	APIC_TIMER_DCR	0x3E0

#define LAPIC_TIMER_IRQ 0

void init_apic(void);
void apic_enable_irq(int irq);

#endif
