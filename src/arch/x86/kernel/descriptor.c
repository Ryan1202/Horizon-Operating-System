#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <kernel/console.h>
#include <drivers/8259a.h>
#include <drivers/pit.h>
#include <kernel/page.h>
#include <kernel/tss.h>
#include <string.h>
#include <config.h>

#define KEYBOARD_IRQ 1
#define IDE0_IRQ 14
#define IDE1_IRQ 15

irq_handler_t irq_table[NR_IRQ];
void default_irq_handler(int irq);
void (*irq_enable)(int);

struct segment_descriptor	*gdt;
struct gate_descriptor		*idt;
static struct tss_s			tss;

void update_tss_esp(struct task_s *pthread)
{
	tss.esp0 = (uint32_t *)((uint32_t)pthread + PAGE_SIZE);
}

void init_descriptor(void)
{
	gdt = (struct segment_descriptor *)GDT_ADDR;
	idt = (struct gate_descriptor *)IDT_ADDR;
	memset(&tss, 0, sizeof(struct tss_s));
	tss.ss0 = SELECTOR_K_STACK;
	tss.io_base = sizeof(struct tss_s);
	
	int i;
	for(i = 0; i < GDT_SIZE / 8; i++)
	{
		set_segmdesc(gdt + i, 0, 0, 0);
	}
	set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, 0x409a);
	set_segmdesc(gdt + 2, 0xffffffff, 0x00000000, 0x4092);
	set_segmdesc(gdt + 3, sizeof(struct tss_s), &tss, DESC_P | DESC_D | DESC_DPL_0 | DESC_S_SYS | DESC_TYPE_TSS);
	set_segmdesc(gdt + 4, 0x000fffff, 0x00000000, DESC_P | DESC_D | DESC_DPL_3 | DESC_S_CODE | DESC_TYPE_CODE);
	set_segmdesc(gdt + 5, 0x000fffff, 0x00000000, DESC_P | DESC_D | DESC_DPL_3 | DESC_S_DATA | DESC_TYPE_DATA);
	
    load_gdtr(GDT_SIZE, GDT_ADDR);
	__asm__ __volatile__ ("ltr %w0" :: "r" (SElECTOR_TSS));
    
    for(i = 0; i < IDT_SIZE / 8; i++)
    {
        set_gatedesc(idt + i, 0, 0, 0);
    }
    set_gatedesc(idt + 0x00, (int)&divide_error, 0x08, DA_386IGate);
	set_gatedesc(idt + 0x01, (int)&single_step_exception, 0x08, DA_386IGate);
	set_gatedesc(idt + 0x02, (int)&nmi, 0x08, DA_386IGate);
	set_gatedesc(idt + 0x0c, (int)&stack_exception, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x0d, (int)&general_protection, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x0e, (int)&page_fault, 0x08, DA_386IGate);
    
	set_gatedesc(idt + 0x20 + LAPIC_TIMER_IRQ, (int)&irq_entry0, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x20 + KEYBOARD_IRQ, (int)&irq_entry1, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x20 + PIT_IRQ, (int)&irq_entry2, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x20 + IDE0_IRQ, (int)&irq_entry14, 0x08, DA_386IGate);
    set_gatedesc(idt + 0x20 + IDE1_IRQ, (int)&irq_entry15, 0x08, DA_386IGate);
    for(i = 0; i < NR_IRQ; i++){
		irq_table[i] = default_irq_handler;
	}
    
    load_idtr(IDT_SIZE, IDT_ADDR);
}

void put_irq_handler(int irq, irq_handler_t handler)
{
    irq_table[irq] = handler;
}

void set_segmdesc(struct segment_descriptor *sd, unsigned int limit, int base, int ar)
{
    if(limit > 0xfffff)
    {
        ar |= 0x8000; /* G_bit = 1 */
        limit /= 0x1000;
    }

    sd->limit_low    = limit & 0xffff;
    sd->base_low     = base & 0xffff;
    sd->base_mid     = (base >> 16) & 0xff;
    sd->access_right = ar & 0xff;
    sd->limit_high   = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
    sd->base_high    = (base >> 24) & 0xff;
    return;
}

/*
 *                                 Task Gate
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |                                   | P | DPL | 00101 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |       TSS Segment Selector        |                                   |
 * -------------------------------------------------------------------------
 * 
 *                               Interrupt Gate
 * 31                               16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |           Offset 31:16            | P | DPL | 0D110 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |         Segment Selector          |            Offset 15:0            |
 * -------------------------------------------------------------------------
 * 
 *                                 Trap Gate
 * 31                                16 15 14  13 12    8 7                0
 * -------------------------------------------------------------------------
 * |           Offset 31:16            | P | DPL | 0D111 |                 |
 * -------------------------------------------------------------------------
 * -------------------------------------------------------------------------
 * |         Segment Selector          |            Offset 15:0            |
 * -------------------------------------------------------------------------
 */

void set_gatedesc(struct gate_descriptor *gd, int offset, int selector, int ar)
{
    gd->offset_low   = offset & 0xffff;
    gd->selector     = selector;
    gd->dw_count     = (ar >> 8) & 0xff;
    gd->access_right = ar & 0xff;
    gd->offset_high  = (offset >> 16) & 0xffff;
    return;
}

void exception_handler(int esp, int vec_no, int err_code, int eip, int cs, int eflags)
{
	char err_description[][64] = {	"#DE Divide Error",
					"#DB RESERVED",
					"—  NMI Interrupt",
					"#BP Breakpoint",
					"#OF Overflow",
					"#BR BOUND Range Exceeded",
					"#UD Invalid Opcode (Undefined Opcode)",
					"#NM Device Not Available (No Math Coprocessor)",
					"#DF Double Fault",
					"    Coprocessor Segment Overrun (reserved)",
					"#TS Invalid TSS",
					"#NP Segment Not Present",
					"#SS Stack-Segment Fault",
					"#GP General Protection",
					"#PF Page Fault",
					"—  (Intel reserved. Do not use.)",
					"#MF x87 FPU Floating-Point Error (Math Fault)",
					"#AC Alignment Check",
					"#MC Machine Check",
					"#XF SIMD Floating-Point Exception"
				};
	io_cli();

	printk(COLOR_RED"ERROR:%s\n",err_description[vec_no]);
	
	printk(COLOR_RED"EFLAGS:%x CS:%x EIP:%x ESP:%x\n",eflags, cs, eip, esp);
	
	if(err_code != 0xFFFFFFFF){
		printk(COLOR_RED"Error code:%x\n", err_code);
		
		if(err_code&1){
			printk(COLOR_RED"    External Event: NMI,hard interruption,ect.\n");
		}else{
			printk(COLOR_RED"    Not External Event: inside.\n");
		}
		if(err_code&(1<<1)){
			printk(COLOR_RED"    IDT: selector in idt.\n");
		}else{
			printk(COLOR_RED"    IDT: selector in gdt or ldt.\n");
		}
		if(err_code&(1<<2)){
			printk(COLOR_RED"    TI: selector in ldt.\n");
		}else{
			printk(COLOR_RED"    TI: selector in gdt.\n");
		}
		printk(COLOR_RED"    Selector: idx %d\n", (err_code&0xfff8)>>3);
	}
    
	io_hlt();
	while(1);
}

void do_interrupt(int irq)
{
	// apic_eoi();
	irq_table[irq](irq);
}

void default_irq_handler(int irq)
{
    
}