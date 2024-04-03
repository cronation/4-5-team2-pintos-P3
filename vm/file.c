/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
bool lazy_load_segment (struct page *page, void *aux);

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

	struct file_info *f_info = &page->file;
	file_page->file = f_info->file;
	file_page->ofs = f_info->offset;
	file_page->read_bytes = f_info->read_bytes;
	file_page->zero_bytes = f_info->zero_bytes;
	
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

	if (pml4_is_dirty(thread_current()->pml4,page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va,0);
	}
	
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	
	struct file *f = file_open(file);

	int total_page_count = (length + PGSIZE - 1) / PGSIZE;
	

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(addr) == 0);      
    ASSERT(offset % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
	
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_info *f_info = malloc(sizeof(struct file_info));

		if (!f_info)
			return false;

		f_info->file = f;
		f_info->offset = offset;
		f_info->read_bytes = page_read_bytes; 
		f_info->zero_bytes = page_zero_bytes; 
	
		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_segment, f_info))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset+= page_read_bytes; 
	}

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {

	struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *p = spt_find_page(spt, addr);
    int count = p->page_count;
    for (int i = 0; i < count; i++)
    {
        if (p)
        	destroy(p);
			
        addr += PGSIZE;
        p = spt_find_page(spt, addr);
    }
}



bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
		struct file_info *f_info = aux;
	
	// 파일의 position을 offset으로 지정
	file_seek(f_info->file, f_info->offset);

	
	// 파일을 read_bytes 만큼 물리 프레임에 읽어들인다.
	if (file_read (f_info->file, page->frame->kva, f_info->read_bytes) != (int) f_info->read_bytes) {
			palloc_free_page (page->frame->kva);
			return false;
		}
		// 다 읽은 지점 부터 zero_bytes 만큼 0으로 채움.
		memset(page->frame->kva + f_info->read_bytes, 0 , f_info->zero_bytes);
	
	return true;
}

struct file *process_get_file (int fd)
{
	struct thread *curr = thread_current ();
	if (fd < 0 || fd >= 63)
		return NULL;

	return curr->fdt[fd];
}
