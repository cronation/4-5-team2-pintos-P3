/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h"
#include "include/userprog/process.h"
#include "devices/disk.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"

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

void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

			
	void *f_addr = addr;
	struct file * r_file = file_reopen(file);
	
	size_t read_bytes = file_length(file) < length ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	while (read_bytes > 0 || zero_bytes > 0)
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lzload_arg *lzl = (struct lzl *)malloc(sizeof(struct lzload_arg));
		lzl->file = r_file;
		lzl->read_bytes = page_read_bytes;
		lzl->ofs = offset;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr,
					writable, lazy_load_segment, lzl))
			return NULL;

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;		
	}
	printf("r_file : %p\n", r_file);
	printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	print_spt();
	return f_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	while (true)
	{
		struct page *page = spt_find_page(&thread_current()->spt, addr);
		if (page == NULL)
			break;
		
		struct lzload_arg * lzl = (struct lzload_arg *)page->uninit.aux;
		if (pml4_is_dirty(thread_current()->pml4, page->va))
		{
			file_write_at(lzl->file, addr, lzl->read_bytes, lzl->ofs);
			pml4_set_dirty (thread_current()->pml4, page->va, 0);
		}

		pml4_clear_page(thread_current()->pml4, page->va);
		addr += PGSIZE;
	}
}
