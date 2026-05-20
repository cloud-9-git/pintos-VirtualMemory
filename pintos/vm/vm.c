/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include <string.h>
#include "userprog/process.h"
#include "vm/file.h"
/* Initializes the virtpual memory subsystem by invoking each subsystem's
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

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_,
		   const struct hash_elem *b_, void *aux UNUSED);

/* type: 만들 페이지의 최종 종류 (VM_ANON, VM_FILE)
 * upage: 이 페이지가 대표하는 유저 가상 주소 
 * writable: PTE 매핑 시 write 권한을 줄지 말지 결정한다 
 * init: page fault로 실제 frame을 붙일 때 내용을 어떻게 채울지 정하는 함수 
 * aux: init이 실행될 때 필요한 부가 데이터 모음 (file, offset, read_bytes, zero_bytes)
 *  */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	struct page *spt_find_result_page = spt_find_page (spt, upage);	

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_result_page == NULL) {
		
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// ASSERT(VM_TYPE(type) == VM_ANON || VM_TYPE(type) == VM_FILE)

		// # TODO: vm_type에 따라 initializer 할당하는 코드 if-else문으로 처리하기
	
		struct page *page = malloc(sizeof(struct page)); 

		uninit_new (page, upage, init, type, aux, VM_TYPE(type) == VM_ANON ? anon_initializer : file_backed_initializer);
		page->writable = writable;
	 
		/* TODO: Insert the page into the spt. */
		bool spt_insert_succeed = spt_insert_page(spt, page); 

		if (spt_insert_succeed) {			
			return true; 
		} else {
			// # TODO: free (혹시 틀렸을 수도 있음)
			free (page);
			goto err; 
		}
	
	} else {		
		goto err; 
	}
	
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct hash_elem *e;
	struct thread *curr_process = thread_current (); 
	
	// # TODO: hash_find() 사용하도록 변경하기
	struct page dummy; 

	dummy.va = pg_round_down (va);

	e = hash_find (&spt->hash_table, &dummy.hash_elem);

	if (e != NULL) {
		page = hash_entry (e, struct page, hash_elem);
	}

	return page != NULL ? page : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page(struct supplemental_page_table *spt, struct page *page)
{
    struct hash_elem *e;
	
	page->va = pg_round_down(page->va); 	

	e = hash_insert(&spt->hash_table, &page->hash_elem);

    return e == NULL;
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
	struct frame *frame = NULL;
	frame = malloc (sizeof (struct frame));
	if (frame == NULL) {
		PANIC ("TODO: Out of Memory - malloc (func: vm_get_frame)");
	}
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL; 
	
	if (frame->kva == NULL) {
		PANIC ("TODO: Out of Memory - palloc (func: vm_get_frame)");
	}

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);	
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	// TODO: 실패시 처리 return으로 충분한지, swap out같은걸 해야 하는지
	bool stack_page_alloc_success = vm_alloc_page (VM_ANON | VM_MARKER_0, addr, true);
	if (stack_page_alloc_success == false) {	
		return;
	}

	bool page_claim_success = vm_claim_page(addr);
	if (page_claim_success == false) {
		return;
	}
	
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;

	/* Validate the fault */
	/* Your code goes here */
	/* # [커널/유저 공통] 읽기 전용 페이지에 쓰려고 한 경우 # */
	if (!not_present) {	
		return false;
	}

	if (user) {
		// 존재하지 않는 va인지는 확인 -> not_present
		if (is_user_vaddr (addr) == false) {	
			return false;
		}
	} else {
		// 커널 주소에 접근하면 kernel bug panic 처리함
		if (is_user_vaddr (addr) == false) {
		 	return false;
		}
		// 커널 영역에서 일어난 일이지만 유저 포인터에 접근하다가 생긴 fault는 anon page를 만들어서 넣어줌
	}
	// TODO: page 구조체에 owner
	page = spt_find_page (spt, addr);
	if (page == NULL) {
		// TODO: stack growth 판단을 넣어줘야 함
		// 	1. 유저 stack영역인지 확인한다
		// 	2. rsp 근처인지 확인한다(이때 rsp는 현재 스레드의 user_rsp필드 안에 들어있는 값을 사용한다)
		// 	3. 커널에서 접근한 걸 때는 rsp값이 달라지므로 fault가 나서 커널모드에 들어오는 순간 저장한 thread 구조체의 필드 user_rsp값을 써야 한다.
		if (is_user_vaddr(addr) && ((thread_current ()->user_rsp) - 8 <= addr) && (addr < USER_STACK) && (addr >= (USER_STACK - STACK_MAX_SIZE))) {
			// 해당 되면 stack_growth한다.
			// TODO: vm_stack_growth 실패할 경우 처리 방법
			vm_stack_growth(addr);
			if (spt_find_page(spt, addr)) {
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	} else {
		return vm_do_claim_page (page);
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
vm_claim_page (void *va) {
	struct page *page = NULL;
	struct thread *curr_process = thread_current(); 
	
	/* TODO: Fill this function */
	page = spt_find_page(&curr_process->spt, va); 
	
	if (page == NULL) {
		return false; 
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: 이 페이지의 가상 주소(VA)가 새로 얻은 
	         frame의 물리 메모리 위치(PA)를 가리키도록 페이지 테이블에 매핑 정보를 등록 */	
	struct thread *curr_process = thread_current();
	bool va_pa_mapping_success = pml4_set_page(curr_process->pml4, page->va, frame->kva, page->writable);
	
	if (!va_pa_mapping_success) {
		return false; 
	}
	
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->hash_table, page_hash, page_less, NULL);
}

static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
	const struct page *p = hash_entry (p_, struct page, hash_elem);
	return hash_bytes (&p->va, sizeof p->va);
}

static bool
page_less (const struct hash_elem *a_,
		   const struct hash_elem *b_, void *aux UNUSED) {
	const struct page *a = hash_entry (a_, struct page, hash_elem);
	const struct page *b = hash_entry (b_, struct page, hash_elem);

	return a->va < b->va;
}

static void 
page_hash_brown_destructor (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry (e, struct page, hash_elem);
	if (page == NULL) {
		printf ("PAGE NULL\n\n");
		return;
	}
	destroy (page);
	free (page);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {

	//for (src buckets를 돌기)
	struct hash_iterator i;
	hash_first(&i, src);

	while (hash_next (&i)) {
    	struct page *source_page = hash_entry (hash_cur  (&i), struct page, hash_elem);
		enum vm_type type;
		vm_initializer *init;
		void *aux = NULL;
		struct file_aux *file_aux = NULL; 

		enum vm_type type_i_love_pintos = source_page->operations->type;

		switch (type_i_love_pintos) {
			case (VM_UNINIT):
				type = source_page->uninit.type;
				init = source_page->uninit.init;
				aux = malloc (sizeof (struct aux));
				memcpy (aux, source_page->uninit.aux, sizeof (struct aux));
				break;
			case (VM_ANON):
				type = source_page->anon.type;
				break;
			case (VM_FILE):			
				type = VM_FILE;			
				init = lazy_load_file;
				file_aux = malloc (sizeof (struct file_aux)); 
				file_aux->file = file_reopen(source_page->file.file);
			    file_aux->offset = source_page->file.offset;
				file_aux->page_read_bytes = source_page->file.page_read_bytes;
				file_aux->start_va = source_page->file.start_va; 
				file_aux->writable = source_page->file.writable;
				break;
			default:
				NOT_REACHED ();
		}
		
		if (type_i_love_pintos == VM_ANON) {
			if (!vm_alloc_page (type, source_page->va, source_page->writable)) {
				return false;
			} else {
				if (!vm_claim_page (source_page->va) ||
					!memcpy (spt_find_page(dst, source_page->va)->frame->kva, source_page->frame->kva, PGSIZE)) {
					return false;
				}
			}
		} 
		else {			
			if (!vm_alloc_page_with_initializer (type, source_page->va, source_page->writable, init, aux)) {
				return false;
			}
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	struct hash_iterator i;

	if (hash_empty (&spt->hash_table)) {
		return;
	}

	hash_first (&i, &spt->hash_table);

	hash_destroy(&spt->hash_table, page_hash_brown_destructor);
	
	return;
}
