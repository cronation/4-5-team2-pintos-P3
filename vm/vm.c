/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/kernel/hash.h"
#include "include/threads/thread.h"
#include "userprog/process.h"



/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

void print_spt(void) {
	struct hash *h = &thread_current()->spt.spt_hash;
	struct hash_iterator i;

	printf("============= {%s} SUP. PAGE TABLE (%d entries) =============\n", thread_current()->name, hash_size(h));
	printf("   USER VA    | KERN VA (PA) |     TYPE     | STK | WRT | DRT(K/U) \n");

	void *va, *kva;
	enum vm_type type;
	char *type_str, *stack_str, *writable_str, *dirty_k_str, *dirty_u_str;
	stack_str = " - ";

	hash_first (&i, h);
	struct page *page;
	// uint64_t *pte;
	while (hash_next (&i)) {
		page = hash_entry (hash_cur (&i), struct page, hash_elem);

		va = page->va;
		if (page->frame) {
			kva = page->frame->kva;
			// pte = pml4e_walk(thread_current()->pml4, page->va, 0);
			writable_str = (uint64_t)page->va & PTE_W ? "YES" : "NO";
			// dirty_str = pml4_is_dirty(thread_current()->pml4, page->va) ? "YES" : "NO";
			// dirty_k_str = is_dirty(page->frame->kpte) ? "YES" : "NO";
			// dirty_u_str = is_dirty(page->frame->upte) ? "YES" : "NO";
		} else {
			kva = NULL;
			dirty_k_str = " - ";
			dirty_u_str = " - ";
		}
		type = page->operations->type;
		if (VM_TYPE(type) == VM_UNINIT) {
			type = page->uninit.type;
			switch (VM_TYPE(type)) {
				case VM_ANON:
					type_str = "UNINIT-ANON";
					break;
				case VM_FILE:
					type_str = "UNINIT-FILE";
					break;
				case VM_PAGE_CACHE:
					type_str = "UNINIT-P.C.";
					break;
				default:
					type_str = "UNKNOWN (#)";
					type_str[9] = VM_TYPE(type) + 48; // 0~7 사이 숫자의 아스키 코드
			}
			// stack_str = type & IS) ? "YES" : "NO";
			struct file_page_args *fpargs = (struct file_page_args *) page->uninit.aux;
			writable_str = (uint64_t)page->va & PTE_W ? "(Y)" : "(N)";
		} else {
			stack_str = "NO";
			switch (VM_TYPE(type)) {
				case VM_ANON:
					type_str = "ANON";
					// stack_str = page->anon.is_stack ? "YES" : "NO";
					break;
				case VM_FILE:
					type_str = "FILE";
					break;
				case VM_PAGE_CACHE:
					type_str = "PAGE CACHE";
					break;
				default:
					type_str = "UNKNOWN (#)";
					type_str[9] = VM_TYPE(type) + 48; // 0~7 사이 숫자의 아스키 코드
			}
			

		}
		printf(" %12p | %12p | %12s | %3s | %3s |  %3s/%3s \n",
			   pg_round_down (va), kva, type_str, stack_str, writable_str, dirty_k_str, dirty_u_str);
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page * use_page;
	bool (*page_initializer)(struct page *, enum vm_type, void *); // uninit struct 안에 이미 있는 initalizer이며, 그대로 가져다 쓰면 됨. 이후 각 type에 맞게 initalizer가 초기화된다.

	// print_spt();
	printf("--VM_ALLOC_PAGE_WITH_INITIALIZER_ACTIVATE--\n");
	// 여기까지는 옴

	/* Check wheter the upage is already occupied or not. */
	// upage가 이미 사용중 or 안  사용중
	if ((use_page = spt_find_page (spt, upage)) == NULL) {
		// printf("[[TRG]]\nLOAD_SEGMENT -> YOU'RE IN VM_ALLOC_PAGE_WITH_INITIALIZER\nSPT_FIND_PAGE\n");
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		switch(VM_TYPE(type)){
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
		}
		struct page * fake_page = calloc(1,sizeof(struct page));
		uninit_new(fake_page, pg_round_down(upage), init, type, aux, page_initializer); // uninit 타입으로 초기화. 깃북을 보면 기본값이 uninit으로 설정되어야 한다는 것을 알 수 있음.
		if (fake_page == NULL){
			printf("FAKE PAGE NULL\n");
		}
		// printf("[[TRG]]\n UNINIT_NEW_COMPLETE\n");

		fake_page->writable = writable;
		// uninit_new함수가 페이지 자체를 초기화시키기 때문에 uninit_new 이후에 수정해야 수정사항이 반양됨.

		/* TODO: Insert the page into the spt. */
		printf("UNINIT_NEW COMPLETE\n");
		spt_insert_page(spt,fake_page);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page * page = NULL;
	page = (struct page*)malloc(sizeof(struct page));
	// va를 넣어줌. 원본 훼손을 막기 위해서 이 작업을 함.
	page -> va = pg_round_down(va);
	/* TODO: Fill this function. */
	
	struct hash_elem * h_elem = hash_find(&spt->spt_hash , &page->hash_elem);
	print_spt();

	if (h_elem == NULL){
		printf("YOU'RE IN SPT FIND PAGE.. h_elem NULL\n");
	}
	page = hash_entry(h_elem, struct page, hash_elem);
	
	free(page);

	printf("[[TRG]]\nRETURN PAGE COMPLETE\n");
	return h_elem != NULL? hash_entry(h_elem, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	// 삽입 성공 시에 NULL 반환, 아니면 elem 포인터 반환
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL){
		printf("HASH INSERT COMPLETE\n");
		succ = true;
	}
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame * frame = (struct frame*)malloc(sizeof(struct frame));
	frame->kva = (struct page*)palloc_get_page(PAL_USER);
	/* TODO: Fill this function. */
	if (frame->kva == NULL){
		frame = vm_evict_frame();
		frame->page = NULL;
		return frame;
	}

	list_push_back(&frame_table, &frame->frame_elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	printf("[[TRG]]\nTRY HANDLE FAULT!\n");

    if (is_kernel_vaddr(addr) && user)
        {
			printf("HANDLE : MUST BE USER\n");
			return false;
		}

	if (not_present){
		printf("HANDLE : IN NOT PRESENT\n");
		page = spt_find_page(spt, pg_round_down(addr));
        if (page == NULL){
			printf("HANDLE : PAGE NULL\n");
            return false;
			}
        if (write == 1 && page->writable == 0){ // write 불가능한 페이지에 write 요청한 경우
            printf("HANDLE : WRITE FAIL\n");
			return false;
		}
		printf("HANDLE : SUCCESS\n");
        return vm_do_claim_page(page);
    }
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	printf("[[TRG]]\nYOU'RE IN VM_CLAIM_PAGE\n");
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, va);
	/* TODO: Fill this function */
	// 추가
	if (va == NULL){
		PANIC("VA MISS");
	}

	page = spt_find_page(&curr->spt , va);
	if (page == NULL){
		PANIC("TODO");
	}
	return vm_do_claim_page (page);
}

static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// printf("YOU'RE IN VM_DO_CLAIM_PAGE\n");
	// 물리주소 하나 가져옴
	printf("[[TRG]]\nYOU'RE IN VM_DO_CLAIM_PAGE\n");
	print_spt();

	struct frame *frame = vm_get_frame();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if (install_page(page->va, frame->kva, page->writable)) {
		return swap_in(page, frame->kva);
	}
	
	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// printf("HASH INIT\n");
	// print_spt(); 
	// 잘 나옴
	hash_init(&spt->spt_hash , page_hash , page_less , NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {

	
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	
	// printf("CLEAR HASH(SPT KILL)\n");
	hash_clear(&spt->spt_hash , clear_hash);
}

void clear_hash(struct hash_elem * h , void * aux)
{
	struct page*page = hash_entry(h,struct page,hash_elem);
	vm_dealloc_page(page);
}

