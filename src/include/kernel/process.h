#ifndef PROCESS_H
#define PROCESS_H

#define USER_STACK3_ADDR 0xffbff000
#define USER_START_ADDR 0X80000000

void start_process(void *filename);
void process_excute(void *filename, char *name);
void page_dir_activate(struct task_s *thread);
void process_activate(struct task_s *thread);

#endif