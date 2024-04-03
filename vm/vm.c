/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/kernel/hash.h"
#include "include/threads/thread.h"
#include "userprog/process.h"

#include "include/threads/interrupt.h"

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
	// printf("ㄴ>VM_ALLOC_PAGE_WITH_INITIALIZER_ACTIVATE\n");
	// 여기까지는 옴
	// printf ("%p in alloc_page_initiallizer\n\n", upage);

	/* Check wheter the upage is already occupied or not. */
	// upage가 이미 사용중 or 안  사용중
	if ((use_page = spt_find_page (spt, upage)) == NULL) {
		switch(VM_TYPE(type)){
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
		}
		struct page * fake_page = malloc(sizeof(struct page));
		uninit_new(fake_page, pg_round_down(upage), init, type, aux, page_initializer); // uninit 타입으로 초기화. 깃북을 보면 기본값이 uninit으로 설정되어야 한다는 것을 알 수 있음.
		if (fake_page == NULL){
			printf("FAKE PAGE NULL\n");
		}
		// printf("[[TRG]]\n UNINIT_NEW_COMPLETE\n");
		fake_page->writable = writable;
		// uninit_new함수가 페이지 자체를 초기화시키기 때문에 uninit_new 이후에 수정해야 수정사항이 반양됨.

		/* TODO: Insert the page into the spt. */
		// printf("    ㄴ> UNINIT_NEW COMPLETE\n");
		if (!spt_insert_page(spt,fake_page)){
			printf("MISS INSERT PAGE\n\n");
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page * page = NULL;
	struct page n_page;
	// va를 넣어줌. 원본 훼손을 막기 위해서 이 작업을 함.
	// printf("  ㄴ> SPT FIND PAGE\n");
	n_page.va = pg_round_down(va);
	/* TODO: Fill this function. */

	if (va == NULL || spt == NULL){
		printf("    => VA : %p / SPT : %p", va,spt);
	}
	
	struct hash_elem * h_elem = hash_find(&spt->spt_hash , &(n_page.hash_elem));

	if (h_elem == NULL){
		// printf("YOU'RE IN SPT FIND PAGE.. h_elem NULL\n");
		return NULL;
	}
	page = hash_entry(h_elem, struct page, hash_elem);

	// printf("[[TRG]]\nRETURN PAGE COMPLETE\n");
	// print_spt();
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	// printf("SPT INSET PAGE\n\n");
	// printf("address : %p\n\n", &page->hash_elem);
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL){
		// printf("HASH INSERT COMPLETE\n");
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
	struct frame * frame = (struct frame *)calloc (sizeof(struct frame), 1);

  frame->kva = palloc_get_page(PAL_USER);

  //(palloc 실패) 만약 가상 공간이 없으면, 기존 페이지를 희생(비운 뒤) 만들어 리턴 (SWAP DISK)
  if (!frame->kva) {
    //TODO Swap Disk
    PANIC(" ------ todo Swap Disk ------ \n");
  }

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	
	if (vm_alloc_page(VM_ANON , pg_round_down(addr) , true))
	{
		vm_claim_page(pg_round_down(addr));
		thread_current()->rsp -= PGSIZE;
	}
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
	struct page * page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// printf("[[TRG]]\nTRY HANDLE FAULT!\n");
	// printf("addr : %p , user : %d , write : %d , not_present : %d\n" , addr , user, write , not_present);
	if (addr == NULL)
        {
			return false;
		}
    if (is_kernel_vaddr(addr))
        {
			return false;
		}

	void * rsp = NULL;
	if (is_kernel_vaddr(f->rsp)){
		// 만약 user_stack이라면 thread에서 가져 올 필요 X
		rsp = thread_current()->rsp;
	}else{
		rsp = f->rsp;
	}

	if (not_present){
		if(!vm_claim_page(addr)) {
			// 여기서 해당 주소가 유저 스택 내에 존재하는지를 체크한다.
			// if (rsp - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK)
			if (rsp - 8 <= addr) {
				vm_stack_growth(thread_current()->rsp - PGSIZE);
				return true;
			}
			return false;
		}
		else{
			return true;
		}

		page = spt_find_page(spt, pg_round_down(addr));
        if (page == NULL){
            return false;
			}
        if (write == 1 && page->writable == 0){ // write 불가능한 페이지에 write 요청한 경우
			return false;
		}
    }
    bool v = vm_do_claim_page(page);
	// print_spt();
	return v;
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
	// printf("[[TRG]]\nYOU'RE IN VM_CLAIM_PAGE\n");
  struct page *page = spt_find_page (&thread_current()->spt, va);

  if (!page) {
    // printf("[ %s ] vm_clame_page : NOT FOUND PAGE { %p }", thread_name (), page);
    return false;
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
	// printf("[[TRG]]\nVM_DO_CLAIM_ACTIVATE\n");
	// print_spt();

	struct frame *frame = vm_get_frame();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if (!install_page(page->va, frame->kva, page->writable)){
			return false;
	}else{
		return swap_in (page, frame->kva);
	}
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// printf("HASH INIT\n");
	// print_spt(); 
	// 잘 나옴
	hash_init(&spt->spt_hash , (void*)page_hash , page_less , NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
			//spt에서 hash, iterator를 받아옴. 이미 struct는 아 내용을 다 했다는 전제 하에 다 있기 때문에 그대로 사용하면 된다.//
			struct hash_iterator iterator;
			struct hash * src_hash = &src->spt_hash;
			struct hash * dst_hash = &dst->spt_hash;

			// hash_first : hash table을 iterator로 순차적으로 순회하기 위해 만든 함수.
			hash_first(&iterator, src_hash);
			while(hash_next(&iterator)){
				// hash_first를 보면 대놓고 이렇게 쓰라고 맨 위에 주석처리까지 되어 있는데 안 할 이유가 없죠?
				struct page * src_page = hash_entry(hash_cur(&iterator), struct page, hash_elem);

				// VM_UNINIT인지 확인한 후(초기화가 안 되었으니까 해 줘야겠죠?), argument들을 담기 위해 기존에 있던 lzload_arg(lazy_load에 필요한 argument 약자, 앞으로 줄여서 lzl이라 한다)를 하나 만들어 준다.
				enum vm_type type = src_page->operations->type;
				if (type == VM_UNINIT){
					// Uninit이면 anon이랑 다르게 초기화도 되지 않은 상태임. 메모리가 실제로 필요할 때만 데이터를 할당하게 된다.
					struct uninit_page * uninit_page = &src_page->uninit;
					struct lzload_arg * lzl = (struct file_loader*)uninit_page->aux;

					// 새로운 lzl을 할당, 기존의 lzl를 복사
					struct lzload_arg * new_lzl = malloc(sizeof(lzl));
					memcpy(new_lzl,uninit_page->aux,sizeof(lzl));
					// 파일을 복제해서 새로운 파일 포인터를 생성해 준다. > 메타데이터와 inode 정보만 써서 새로운 파일 객체를 만들어 주기 때문에 효율적인 함수이다.
					new_lzl->file = file_duplicate(lzl->file);

					// 이후 uninit_new 호출해서 초기화 안 된 페이지를 만들어 주고 이걸 spt에 추가하는 함수(load_segment에도 있는 함수이다.)
					vm_alloc_page_with_initializer(uninit_page->type,src_page->va,true,uninit_page->init,new_lzl);
					// SPT 찾아서 주어진 va에 맞는 페이지를 할당하고 매핑함. PA - VA 확인 -> 매핑
					vm_claim_page(src_page->va);
					// 로드는 시키지 않음. 필요할 때 초기화시키고 매핑함.
				}else{
					// UNINIT이 아니니, load 및 초기화가 되지 않은 페이지이므로 바로 초기화하고 메모리에 매핑시킴
					vm_alloc_page(src_page->operations->type, src_page->va,true);
					memcpy(src_page->va, src_page->frame->kva,PGSIZE);
				}
			}
	return true;
}

void spt_dest(struct hash_elem * e, void * aux){
	struct page * page = hash_entry(e,struct page, hash_elem);
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash , clear_hash);
	// struct hash_iterator i;
	// hash_first(&i, &spt->spt_hash);
	// while (hash_next(&i)){
	// 	struct page * page = hash_entry(hash_cur(&i), struct page, hash_elem);
	// 	if (page->operations->type == VM_FILE){
	// 		do_munmap(page->va);
	// 	}
	// }
	// hash_destroy(&spt->spt_hash, spt_dest);
}

void clear_hash(struct hash_elem * h , void * aux)
{
	struct page*page = hash_entry(h,struct page,hash_elem);
	if (page != NULL){
	vm_dealloc_page(page);
	}
}

