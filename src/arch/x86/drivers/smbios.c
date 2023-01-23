/**
 * @file smbios.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 临时用的SMBIOS驱动
 * @version 0.1 Alpha
 * @date 2020-10
 */

#include <drivers/smbios.h>
#include <kernel/console.h>

struct SMBIOSEntryPoint *EntryPoint;
unsigned int             smbios_tables[256];

void init_smbios(void)
{
    unsigned char         *mem = (unsigned char *)0xf0000;
    unsigned char          checksum;
    struct BIOS_Info      *bios;
    struct System_info    *system;
    struct Baseboard_info *baseboard;
    struct Processor_info *processor;
    int                    i;
    while ((unsigned int)mem < 0x100000) {
        if (mem[0] == '_' && mem[1] == 'S' && mem[2] == 'M' && mem[3] == '_') {
            for (i = 0; i < mem[5]; i++) {
                checksum += mem[i];
            }
            if (checksum == 0)
                break;
        }
        mem += 16;
    }
    EntryPoint = (struct SMBIOSEntryPoint *)mem;

    struct SMBIOSHeader *header = EntryPoint->TableAddress;
    mem                         = EntryPoint->TableAddress;
    while (mem < EntryPoint->TableAddress + EntryPoint->TableLength) {
        smbios_tables[header->Type] = (unsigned int *)mem;
        switch (header->Type) {
        case 0:
            bios = (struct BIOS_Info *)mem;
            break;
        case 1:
            system = (struct System_info *)mem;
            break;
        case 2:
            baseboard = (struct Baseboard_info *)mem;
            break;
        case 4:
            processor = (struct Processor_info *)mem;
            printk(COLOR_AQUA "\nCPU Max Speed: %dMHz\n", processor->MaxSpeed);
            printk(COLOR_AQUA "\nCPU Speed: %dMHz\n", processor->CurrentSpeed);
            break;

        default:
            break;
        }
        mem += header->Length;
        while (mem - (unsigned char *)header < EntryPoint->TableLength - 1 && (mem[0] || mem[1]))
            mem++;
        mem += 2;
        header = mem;
    }
}