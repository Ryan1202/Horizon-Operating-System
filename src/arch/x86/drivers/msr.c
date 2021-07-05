#include <device/msr.h>

// char cpuHasMSR(void)
// {
//    uint32_t a, d; // eax, edx
//    cpuid(1, &a, &d);
//    return d & 0x20;
// }

void cpu_RDMSR(uint32_t msr, uint32_t *lo, uint32_t *hi)
{
   uint32_t l,h;
   __asm__ __volatile__("rdmsr \n\t" : "=a"(l),"=d"(h) : "c"(msr):"memory");
   *lo = l;
   *hi = h;
}
 
void cpu_WRMSR(uint32_t msr, uint32_t lo, uint32_t hi)
{
   __asm__ __volatile__("wrmsr \n\t" : : "a"(lo), "d"(hi), "c"(msr):"memory");
}