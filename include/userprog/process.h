#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct file_info {
    struct file *file;
    uint32_t read_bytes;
    off_t offset;
    uint32_t zero_bytes;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

void argument_passing(char *file_name,int count,void **rsp);

#endif /* userprog/process.h */
