#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "lib/kernel/console.h"
#include "lib/kernel/stdio.h"
#include "lib/string.h"
#include "vm/vm.h"

struct lock filesys_lock;
typedef int pid_t;
void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}



/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	switch (f->R.rax)
	{
	case SYS_HALT:{
		halt();
		break;
	}
	case SYS_EXIT:{
		exit((int)f->R.rdi);
		break;
	}
	case SYS_MMAP:{
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
        break;
	}

	case SYS_MUNMAP:{
		munmap(f->R.rdi);
		break;
	}
	case SYS_FORK:{

		pid_t ppid = ffork ((const char *)f->R.rdi, f); //이름이 내장함수에 충돌된다고 컴파일이 울어서 고쳐줌.
		f->R.rax = ppid;
		break;
	}

	case SYS_EXEC:{
		int success = exec((const char *)f->R.rdi);
		f->R.rax = success;
		break;
	}
	
	case SYS_WAIT:{
		int status = wait((pid_t)f->R.rdi);
		f->R.rax = status;
		break;
	}
	case SYS_CREATE:{
		bool is_create = create((const char *)f->R.rdi, (unsigned int) f->R.rsi);
		f->R.rax = is_create;
		break;
	}
	case SYS_REMOVE:{
		bool is_remove = remove((const char *)f->R.rdi);
		f->R.rax = is_remove;
		break;
	}
	case SYS_OPEN:{
		int fd = open((const char *)f->R.rdi);
		f->R.rax = fd;
		break;
	}

	case SYS_FILESIZE:{
		int size = filesize((int)f->R.rdi);
		f->R.rax = size;
		break;
	}
	case SYS_READ:{
		int byte_read = read((int)f->R.rdi, (void *)f->R.rsi, (unsigned int) f->R.rdx);
		f->R.rax = byte_read;
		break;
	}
	case SYS_WRITE:{
		int byte_write = write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned int)f->R.rdx);
		f->R.rax = byte_write;
		break;
	}
	case SYS_SEEK:{
		seek((int)f->R.rdi, (unsigned int)f->R.rsi);
		break;
}
	case SYS_TELL:{
		unsigned int position = tell((int)f->R.rdi);
		f->R.rax = position;
		break;
	}
	case SYS_CLOSE:{
		close((int)f->R.rdi);
		break;
		}
	default:
		exit(-1);
		break;
	}
}



pid_t 
ffork (const char *thread_name, struct intr_frame *f){


	return process_fork(thread_name, f);

}

void
check_address(const uint64_t *addr){


	if(addr == NULL || !is_user_vaddr(addr)){
		exit(-1);
	}

	struct page *page = spt_find_page(&thread_current()->spt,addr);

	if (page == NULL)
	{
		exit(-1);
	}
}


void
halt (void) {
	power_off();
}

void
exit (int status) {
	// pid에 exit status 저장
	// 딱히 pid 가 없어서 thread... 안에 저장할까싶다
	struct thread *curr = thread_current();
	// printf("exit cur name : %s\n", curr->name);
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int
exec (const char *file) {
	check_address(file);
	tid_t tid;
	char * f_name_copy;
	f_name_copy = palloc_get_page(PAL_ZERO);
	if(f_name_copy == NULL){
		exit(-1);
	}
	strlcpy(f_name_copy, file, strlen(file)+1);

	tid = process_exec((void *)f_name_copy);
	if(tid == -1){
		exit(-1);
	}

}

int
wait (pid_t pid) {

	return process_wait(pid);

}

bool
create (const char *file, unsigned initial_size) {
	check_address(file);
	lock_acquire(&filesys_lock);
	bool is_create = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return is_create;

}

bool
remove (const char *file) {
	check_address(file);
	// 바로 삭제하지 않고 열려있다면 그 파일은 close가 되지 않도록 처리.
	lock_acquire(&filesys_lock);
	bool is_remove = filesys_remove(file);
	lock_release(&filesys_lock);
	return is_remove;

}

int
open (const char *file) {
	check_address(file);
	lock_acquire(&filesys_lock);
	struct file *open_n = filesys_open(file);
	lock_release(&filesys_lock);
	if(open_n == NULL){
		return -1;
	}
	// printf("open thread_current name : %s\n", thread_current()->name);
	int new_fd;
	new_fd = thread_current()->next_fd;
	if(new_fd == 63){
		return -1;
	}
	thread_current()->next_fd += 1;
	// printf("open next_fd : %d\n", thread_current()->next_fd);
	// printf("open new_fd :%d\n",new_fd);
	if(new_fd <2){
		new_fd = 2; // 0이하가 인식 안되어 꼼수.
		thread_current()->next_fd = 3;
	}
	thread_current()->fdt[new_fd] = open_n;

	return new_fd;
}

int
filesize (int fd) {

	struct file *target_file = thread_current()->fdt[fd];
	lock_acquire(&filesys_lock);
	off_t size = file_length(target_file);
	lock_release(&filesys_lock);
	return size;
}

int
read (int fd, void *buffer, unsigned size) {

	check_address(buffer);
	struct page *page = spt_find_page(&thread_current()->spt,buffer);
	if (!page->writable)
		exit(-1);

	lock_acquire(&filesys_lock);
	if(fd == 0){
		unsigned count = size;
		while(count--)
			*((char *)buffer++)= input_getc();
		lock_release(&filesys_lock);
		return size;
	}
	else if(fd == 1){
		lock_release(&filesys_lock);
		return -1;
	}
	
	if(fd <64){
		struct file *target_file = thread_current()->fdt[fd];
		off_t byte_read =  file_read(target_file,buffer,size);
		lock_release(&filesys_lock);
		return byte_read;
		}
	else{
		exit(-1);
		// 64 이상이나 이하의 fd를 가져왔다면 리턴.
		// 또 target_file 자체가 존재하는지도 확인해야함.
	}



}


int
write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	struct page *page = spt_find_page(&thread_current()->spt,buffer);
	if (!page->writable)
		exit(-1);
	lock_acquire(&filesys_lock);

	if(fd >64 || fd <0){
		lock_release(&filesys_lock);
		exit(-1);

	}

	if(fd == 1){
		// const char *ptr = (const char *)buffer;
		// for (unsigned i = 0; i < size; i++) {
		// 	if (*ptr == '\0') {
		// 		// null 포인터를 발견한 경우
		// 		exit(-1);
		// 	}
		// 	  printf("write buffer : %s\n", ptr);
        // 	ptr++; // 다음 바이트로 이동
		// }
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	else if(fd == 0){
		lock_release(&filesys_lock);
		return -1;
	}
	else if(!is_user_vaddr(buffer+size)){
		lock_release(&filesys_lock);
		exit(-1);
	}
	else{
		struct file *target_file = thread_current()->fdt[fd];
		if(target_file == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		off_t byte_write = file_write(target_file,buffer,size);
		lock_release(&filesys_lock);
		return byte_write;
	}
	// 뭘해도 write bad ptr이 안 낫네..
}

void
seek (int fd, unsigned position) {

	struct file *target_file = thread_current()->fdt[fd];
	if(position > (unsigned int) filesize(fd)){
		return ;
	}
	else{
		lock_acquire(&filesys_lock);
		file_seek(target_file, position);
		lock_release(&filesys_lock);
	}
}

unsigned
tell (int fd) {
	struct file *target_file = thread_current()->fdt[fd];
	lock_acquire(&filesys_lock);
	off_t position = file_tell(target_file);
	lock_release(&filesys_lock);
	return (unsigned)position;
}

void
close (int fd) {

	struct file *target_file;
	if(fd >64 || fd <0){
		exit(-1);
	}
	if(thread_current()->fdt[fd] == NULL){
		exit(-1);
	}
	target_file = thread_current()->fdt[fd];

	if(target_file == NULL){
		return 0;
	}

	thread_current()->fdt[fd] = NULL; // 닫았으니 null로 초기화 
	lock_acquire(&filesys_lock);
	file_close(target_file);
	lock_release(&filesys_lock);
}

int dup2(int oldfd, int newfd){

}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{

	if (!addr || addr != pg_round_down(addr))
		return NULL;

    if (offset != pg_round_down(offset))
        return NULL;

    if (!is_user_vaddr(addr) || !is_user_vaddr(addr + length))
        return NULL;

    if (spt_find_page(&thread_current()->spt, addr))
        return NULL;

    struct file *f = process_get_file(fd);
    if (f == NULL)
        return NULL;

    if (file_length(f) == 0 || (int)length <= 0)
        return NULL;

    return do_mmap(addr, length, writable, f, offset);
}

void munmap(void *addr)
{
    do_munmap(addr);
}