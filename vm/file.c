/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "include/threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
static bool
lazy_load_segment (struct page *page, void *aux) {
	
	struct lzload_arg * lzl = aux;	
	file_seek(lzl->file , lzl->ofs);
	if (file_read(lzl->file,page->frame->kva,lzl->read_bytes) != (int)(lzl->read_bytes)){
		palloc_free_page(page->frame->kva);
		return false;
	}

	memset(page->frame->kva + lzl->read_bytes, 0, lzl->zero_bytes);
	return true;
}

void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
			
		struct file * dup_file = file_reopen(file);

		void * first_addr = addr;
		int total_page_count = NULL;
		if (length <= PGSIZE){
			total_page_count = 1;
		}else if (length % PGSIZE == 0){
			total_page_count = (length/PGSIZE);
		}else{
			total_page_count = (length/PGSIZE) + 1;
		}

		size_t read_bytes = length < file_length(dup_file);
		size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

		while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lzload_arg * lzl = malloc(sizeof(struct lzload_arg));
		lzl->file = file;
		lzl->ofs = offset;
		lzl->read_bytes = page_read_bytes;
		lzl->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_ANON, addr,
					writable, lazy_load_segment, lzl)){
			printf("[[TRG]]\nWITH_INITIALIZER_FALSE --> 이거 false되면 안 됨\n");
			printf("BEFORE INITIALIZER FALSE -> PGSIZE : %d , page_read_bytes : %d , page_zero_bytes : %d\n", length, page_read_bytes, page_zero_bytes);
			return NULL;
			}

		struct page * p = spt_find_page(&thread_current()->spt , first_addr);
		p->mapped_page_count = total_page_count;

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += length;
		offset += page_read_bytes;
	}
	return first_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	
}
