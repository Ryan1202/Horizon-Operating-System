#ifndef _8259A_H
#define _8259A_H

#define PIC0_ICW1 0x20
#define PIC0_OCW1 0x20
#define PIC0_IMR  0x21
#define PIC0_ICW2 0x21
#define PIC0_ICW3 0x21
#define PIC0_ICW4 0x21
#define PIC1_ICW1 0xa0
#define PIC1_OCW1 0xa0
#define PIC1_IMR  0xa1
#define PIC1_ICW2 0xa1
#define PIC1_ICW3 0xa1
#define PIC1_ICW4 0xa1

#define PIC_EOI 0x20

extern struct Device pic_device;

void mask_8259a(void);
void register_pic(void);

#endif