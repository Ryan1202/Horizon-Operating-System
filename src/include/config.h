#ifndef _CONFIG_H
#define _CONFIG_H

//#define APIC

#ifdef APIC

#include <drivers/apic.h>

#else

#include <drivers/8259a.h>

#endif

extern char use_apic;

#endif