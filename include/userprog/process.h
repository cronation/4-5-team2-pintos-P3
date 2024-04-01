#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* Project 2 - Argument Passing */
static void argument_passing (struct intr_frame *if_, int argv_cnt, char **argv_list);

/* project 3 - VM */
static void start_process(void*file_name_);
void process_exit(void);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable);

static bool
lazy_load_segment (struct page *page, void *aux);

#endif /* userprog/process.h */
