#ifndef _CONFIG_H
#define _CONFIG_H

#define APIC

#ifdef APIC

#include <device/apic.h>

#define INIT_PIC()      init_apic()
#define irq_enable(irq) apic_enable_irq(irq)

#else

#include <device/8259a.h>

#define INIT_PIC() init_8259a()
#define irq_enable(irq) enable_irq(irq)

#endif

#endif