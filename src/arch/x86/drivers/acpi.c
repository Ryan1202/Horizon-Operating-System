#include <drivers/acpi.h>
#include <drivers/pit.h>
#include <kernel/func.h>
#include <kernel/page.h>
#include <kernel/initcall.h>
#include <kernel/driver.h>
#include <kernel/console.h>
#include <string.h>

struct ACPI_RSDP *RSDP;

#define DRV_NAME "Advanced Configuration Power Interface Driver"
#define DEV_NAME "ACPI"

static status_t acpi_enter(driver_t *drv_obj);
static status_t acpi_exit(driver_t *drv_obj);

typedef struct
{
	struct ACPI_RSDT *RSDT;
	struct ACPI_FADT *FADT;
} device_extension_t;

driver_func_t acpi_driver = {
	.driver_enter = acpi_enter,
	.driver_exit = acpi_exit,
	.driver_open = NULL,
	.driver_close = NULL,
	.driver_read = NULL,
	.driver_write = NULL,
	.driver_devctl = NULL
};

char checksum(uint8_t *addr, uint32_t length)
{
    unsigned char sum = 0;
 
    for (int i = 0; i < length; i++)
    {
        sum += ((char *) addr)[i];
    }
 
    return sum == 0;
}

uint32_t *acpi_find_rsdp(void)
{
	uint32_t *addr;
	
	for (addr = 0x000e0000; addr < 0x00100000; addr++)
	{
		if (memcmp(addr, "RSD PTR ", 8) == 0)
		{
			if (checksum(addr, ((struct ACPI_RSDP *)addr)->Length))
			{
				return addr;
			}
		}
	}
	return NULL;
}

uint32_t acpi_find_table(device_extension_t *devext, char *Signature)
{
	int i, length = (devext->RSDT->header.Length - sizeof(devext->RSDT->header))/4;
	struct ACPISDTHeader *header;
	for (i = 0; i < length; i++)
	{
		header = remap(devext->RSDT->Entry + i*4, sizeof(struct ACPISDTHeader));
		if (memcmp(header->Signature, Signature, 4) == 0)
		{
			return (uint32_t)header;
		}
	}
}

static status_t acpi_enter(driver_t *drv_obj)
{
	device_t *devobj;
	device_extension_t *devext;
	
	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_MANAGER, &devobj);
	devext = devobj->device_extension;
	
	RSDP = (struct ACPI_RSDP *)acpi_find_rsdp();
	if (RSDP == NULL) return;
	unsigned int *ptr = remap(RSDP->RsdtAddress, sizeof(struct ACPI_RSDT));
	devext->RSDT = ptr;
	// checksum(RSDT, RSDT->header.Length);
	if (!checksum(devext->RSDT, devext->RSDT->header.Length)) return;
	
	int length = devext->RSDT->header.Length;
	devext->FADT = acpi_find_table(devext, "FACP");
	if (!checksum((uint32_t *)devext->FADT, devext->FADT->h.Length)) return;
	
	if (!(io_in16(devext->FADT->PM1aControlBlock) & 1))
	{
		if (devext->FADT->SMI_CommandPort && devext->FADT->AcpiEnable)
		{
			io_out8(devext->FADT->SMI_CommandPort, devext->FADT->AcpiEnable);
			int i;
			for (i = 0; i < 300; i++)
			{
				if (io_in16(devext->FADT->PM1aControlBlock) & 1) break;
				delay(1);
			}
			if (devext->FADT->PM1bControlBlock)
			{
				for (; i < 300; i++)
				{
					if (io_in16(devext->FADT->PM1bControlBlock) & 1) break;
					delay(1);
				}
			}
		}
	}
	return SUCCUESS;
}

/*
 * \_S5 Object 
 * -----------------------------------
 * NameOP | \(可选) | _  | S  | 5  | _
 * 08     | 5A     | 5F | 53 | 35 | 5F
 * -----------------------------------
 * PackageOP | PkgLength | NumElements | prefix Num | prefix Num | prefix Num | prefix Num
 * 12        | 0A        | 04          | 0A     05  | 0A     05  | 0A     05  | 0A     05
 * -----------------------------------
 * PkgLength: bit6~7为长度的字节数-1;bit4~5保留;bit0~3为长度的低4位
 * prefix:	0A Byte
 * 			0B Word
 * 			0C DWord
 * 			0D String
 * 			0E Qword
*/

void acpi_shutdown(device_extension_t *devext)
{	
	int i;
	uint16_t SLP_TYPa, SLP_TYPb;
	struct ACPISDTHeader *header = acpi_find_table(devext, "DSDT");
	char *S5Addr = (char *)header;
	int dsdtLength = (header->Length - sizeof(struct ACPISDTHeader))/4;
	
	for(i = 0; i < dsdtLength; i++)
	{
		if (memcmp(S5Addr, "_S5_", 4) == 0) break;
		S5Addr++;
	}
	if (i < dsdtLength)
	{
		if ( ( *(S5Addr-1) == 0x08 || ( *(S5Addr-2) == 0x08 && *(S5Addr-1) == '\\') ) && *(S5Addr+4) == 0x12 )
		{
			S5Addr+=5;
			S5Addr+=((*S5Addr&0xc0)>>6)+2;
			
			if (*S5Addr == 0x0a) S5Addr++;
			SLP_TYPa = *(S5Addr)<<10;
			S5Addr++;
			
			if (*S5Addr == 0x0a) S5Addr++;
			SLP_TYPb = *(S5Addr)<<10;
			S5Addr++;
		}
		// 关于PM1x_CNT_BLK的描述见 ACPI Specification Ver6.3 4.8.3.2.1
		io_out16(devext->FADT->PM1aControlBlock, SLP_TYPa | 1<<13);
		if (devext->FADT->PM1bControlBlock != 0)
		{
			io_out16(devext->FADT->PM1bControlBlock, SLP_TYPb | 1<<13);
		}
	}
}

static status_t acpi_exit(driver_t *drv_obj)
{
	device_t *devobj, *next;
	// device_extension_t *ext;
	list_for_each_owner_safe(devobj, next, &drv_obj->device_list, list)
	{
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void acpi_driver_entry(void)
{
	if (driver_create(acpi_driver, DRV_NAME) < 0)
	{
		printk(COLOR_RED"[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(acpi_driver_entry);
