#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H


#include <stdbool.h>
#include <debug.h>
#include <stddef.h>
#include <threads/synch.h>
#include "threads/interrupt.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Map region identifier. */
typedef int off_t;
#define MAP_FAILED ((void *) NULL)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0          /* Successful execution. */
#define EXIT_FAILURE 1          /* Unsuccessful execution. */

struct file_elem {
	struct file *file;
	int fd;
	struct list_elem elem;
};

void syscall_init (void);

void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
void check_address(const uint64_t *addr);



struct file* get_file_fd(int fd);

pid_t ffork (const char *thread_name, struct intr_frame *f);

int dup2(int oldfd, int newfd);

#endif /* userprog/syscall.h */
