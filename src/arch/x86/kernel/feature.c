#include <bits.h>
#include <kernel/console.h>
#include <kernel/feature.h>
#include <kernel/func.h>
#include <stdint.h>

char			vendor_id[13]  = {0};
char			model_name[49] = {0};
static uint32_t max_basic_func_num;
static uint8_t	cpu_family, cpu_model, cpu_stepping_id;
static uint8_t	processor_type, brand_index;
static uint8_t	default_apic_id, max_logical_processor_per_package;
static uint16_t cache_line_size;
static uint32_t feature_flags[2];

const char feature_names[64][16] = {
	"SSE3",	  "PCLMUL",		"DTES64",  "MONITOR", "DS-CPL",
	"VMX",	  "SMX",		"EST",	   "TM2",	  "SSSE3",
	"CID",	  "",			"FMA",	   "CX16",	  "XTPR",
	"PDCM",	  "",			"PCID",	   "DCA",	  "SSE4.1",
	"SSE4.2", "x2APIC",		"MOVBE",   "POPCNT",  "TSC-DEADLINE",
	"AES",	  "XSAVE",		"OSXSAVE", "AVX",	  "F16C",
	"RDRAND", "HYPERVISOR",

	"FPU",	  "VME",		"DE",	   "PSE",	  "TSC",
	"MSR",	  "PAE",		"MCE",	   "CX8",	  "APIC",
	"",		  "SEP",		"MTRR",	   "PGE",	  "MCA",
	"CMOV",	  "PAT",		"PSE36",   "PSN",	  "CLFLUSH",
	"",		  "DS",			"ACPI",	   "MMX",	  "FXSR",
	"SSE",	  "SSE2",		"SS",	   "HTT",	  "TM",
	"",		  "PBE",
};

void read_features(void) {
	uint32_t a, b, c, d;
	get_cpuid(
		0, 0, &max_basic_func_num, (uint32_t *)&vendor_id[0],
		(uint32_t *)&vendor_id[8], (uint32_t *)&vendor_id[4]);
	get_cpuid(1, 0, &a, &b, &c, &d);
	cpu_family		= (a >> 8) & 0x0f + (a >> 20) & 0xff;
	cpu_model		= ((a >> 4) & 0x0f) | (((a >> 16) & 0x0f) << 4);
	cpu_stepping_id = a & 0x0f;
	processor_type	= (a >> 12) & 0x03;

	default_apic_id					  = b >> 24;
	max_logical_processor_per_package = (b >> 16) & 0xff;
	cache_line_size					  = ((b >> 8) & 0xff) * 8;
	brand_index						  = b & 0xff;

	feature_flags[0] = c;
	feature_flags[1] = d;

	get_cpuid(
		0x80000002, 0, (uint32_t *)&model_name[0], (uint32_t *)&model_name[4],
		(uint32_t *)&model_name[8], (uint32_t *)&model_name[12]);
	get_cpuid(
		0x80000003, 0, (uint32_t *)&model_name[16], (uint32_t *)&model_name[20],
		(uint32_t *)&model_name[24], (uint32_t *)&model_name[28]);
	get_cpuid(
		0x80000003, 0, (uint32_t *)&model_name[32], (uint32_t *)&model_name[36],
		(uint32_t *)&model_name[40], (uint32_t *)&model_name[44]);
}

int cpu_check_feature(enum x86_cpu_features feature) {
	if (feature >= 32) {
		return !!(feature_flags[1] & BIT(feature % 32));
	} else {
		return !!(feature_flags[0] & BIT(feature));
	}
}

void print_features() {
	printk("CPU Infomations:\n");
	printk("Vendor: %s,", vendor_id);
	printk("Model: %s\n", model_name);
	printk("Family: %d,", cpu_family);
	printk("Model: %d,", cpu_model);
	printk("Stepping ID: %d\n", cpu_stepping_id);
	printk("Processor Type: %d,", processor_type);
	printk("Default APIC ID: %d\n", default_apic_id);
	printk(
		"Max Logical Processor Per Package: %d\n",
		max_logical_processor_per_package);
	printk("Cache Line Size: %dBytes\n", cache_line_size);
	printk("Brand Index: %d\n", brand_index);
	printk("Features: ");
	for (int i = 0; i < 64; i++) {
		if (cpu_check_feature(i)) { printk("%s, ", feature_names[i]); }
	}
	printk("\n");
}
