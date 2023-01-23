#ifndef PROCESS_H
#define PROCESS_H

#include <kernel/app.h>

#define USER_STACK3_ADDR 0xffbff000
#define USER_START_ADDR  0X80000000

void start_process(void *filename);
void process_excute(void *filename, struct program_struct *prog);
void page_dir_activate(struct task_s *thread);
void process_activate(struct task_s *thread);
int  process_load_segment(struct task_s *thread, struct index_node *inode, unsigned long offset, unsigned long filesz, unsigned long memsz, unsigned long vaddr);

#endif