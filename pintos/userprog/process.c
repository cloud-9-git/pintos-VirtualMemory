#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static bool parse_filename(const char *file_name, char *tmp, size_t tmp_size);
static void child_status_release (struct child_status *child);
static struct child_status *child_status_create(void);

struct child_status {
	tid_t tid;                     /* 자식 스레드 tid */
	int exit_status;               /* 자식이 exit()할 때 남긴 종료 코드 */
	bool exited;                   /* 자식이 종료했는지 여부 */
	bool waited;                   /* 부모가 자식에 대해서 wait() 했는지 여부 */
	int ref_cnt;
	struct lock lock;
	struct semaphore wait_sema;    /* 부모가 자식 종료를 기다릴 때 사용하는 세마포어 */
	struct list_elem elem;		   /* child list 리스트 노드 */
};

struct fork_aux {
	struct thread *parent;	// fork 를 호출한 부모 스레드
	struct intr_frame parent_if;	// fork 시점의 부모 실행 문맥 복사본
	struct  child_status *child_status;	// 부모와 자식이 공유할 자식 상태표
	struct semaphore fork_sema;	// 자식의 fork 초기화 완료를 부모에게 알리는 세마포어
	bool success;	// 자식의 주소공간/자원 복사 성공 여부
};

struct initd_aux {
	char *file_name;
	struct child_status *child_status;
};

/* initd와 그 외 프로세스에서 공통으로 사용하는 초기화 함수. */
static void
process_init (void) {
	struct thread *current = thread_current ();

	list_init(&current->child_list);
	current->exec_file = NULL;
	current->my_status = NULL;
}

/* FILE_NAME에서 읽어들인 첫 번째 사용자 영역 프로그램 "initd"를 시작한다.
 * process_create_initd()가 반환되기 전에 새 스레드가 스케줄될 수도 있고,
 * 심지어 종료될 수도 있다. 성공하면 initd의 스레드 ID를 반환하고,
 * 스레드를 만들 수 없으면 TID_ERROR를 반환한다.
 * 이 함수는 반드시 한 번만 호출해야 한다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	struct thread *cur = thread_current ();
	struct child_status *child;
	struct initd_aux *aux;

	/* FILE_NAME의 사본을 만든다.
	 * 그렇지 않으면 호출자와 load() 사이에 경쟁 상태가 생긴다. */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *tmp = palloc_get_page(0);
	if (tmp == NULL) {
		palloc_free_page(fn_copy);
		return TID_ERROR;
	}

	if (!parse_filename(file_name, tmp, PGSIZE)) {
		palloc_free_page(tmp);
		palloc_free_page(fn_copy);
		return TID_ERROR;
	}

	/* initd도 부모 입장에서는 기다려야 하는 자식 프로세스다.
	 * fork 자식과 같은 방식으로 child_status를 만들어 child_list에 등록한다. */
	child = child_status_create ();
	if (child == NULL) {
		palloc_free_page(tmp);
		palloc_free_page(fn_copy);
		return TID_ERROR;
	}

	aux = malloc (sizeof *aux);
	if (aux == NULL) {
		free(child);
		palloc_free_page(tmp);
		palloc_free_page(fn_copy);
		return TID_ERROR;
	}

	aux->file_name = fn_copy;
	aux->child_status = child;

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (tmp, PRI_DEFAULT, initd, aux);
	palloc_free_page(tmp);
  
	if (tid == TID_ERROR) {
		free(aux);
		free(child);
		palloc_free_page (fn_copy);
		return TID_ERROR;
	}

	child->tid = tid;
	list_push_back(&cur->child_list, &child->elem);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *aux_) {
	struct initd_aux *aux = aux_;
	char *file_name = aux->file_name;
	struct child_status *status = aux->child_status;
  
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();
	/* process_wait()를 호출한 부모와 종료 상태를 공유할 수 있게 연결한다. */
	thread_current()->my_status = status;
	free(aux);

	if (process_exec (file_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

static bool parse_filename(const char *file_name, char *tmp, size_t tmp_size) {
	if (tmp == NULL || file_name == NULL) {
		return false;
	}

	strlcpy(tmp, file_name, tmp_size);

	char *save_token;
	char *token = strtok_r(tmp, " ", &save_token);
	
	return token != NULL;
}

/* 현재 프로세스를 `name`이라는 이름으로 복제한다. 성공하면 새 프로세스의
 * 스레드 ID를 반환하고, 스레드를 만들 수 없으면 TID_ERROR를 반환한다. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	struct thread *cur = thread_current();
	struct child_status *child = child_status_create();
	if (child == NULL) {
		return TID_ERROR;
	}

	struct fork_aux *aux = malloc(sizeof *aux);
	if (aux == NULL) {
		free(child);
		return TID_ERROR;
	}

	aux->parent = cur;
	memcpy(&aux->parent_if, if_, sizeof *if_);
	aux->child_status = child;
	aux->success = false;
	sema_init(&aux->fork_sema, 0);

	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, aux);
	palloc_free_page((void *)name);
		
	if (tid == TID_ERROR) {
		free(aux);
		free(child);
		return TID_ERROR;
	}
	  
	child->tid = tid;
	list_push_back(&cur->child_list, &child->elem);
	sema_down(&aux->fork_sema);
		
	if (!aux->success) {
		list_remove(&child->elem);
		child_status_release(child);
		free(aux);
		return TID_ERROR;
	}
	free(aux);
	return child->tid;
}

static struct child_status *child_status_create(void) {
	struct child_status *child = malloc(sizeof *child);
	if (child == NULL) {
		return NULL;
	}
	child->tid = TID_ERROR;
	child->exit_status = 0;
	child->exited = false;
	child->waited = false;
	child->ref_cnt = 2;
	lock_init(&child->lock);
	sema_init(&child->wait_sema, 0);

	return child;
}

#ifndef VM
/* 이 함수를 pml4_for_each에 넘겨 부모의 주소 공간을 복제한다.
 * project 2에서만 사용한다. */

static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable = is_writable (pte);

	/* 1. TODO: parent_page가 커널 페이지라면 즉시 반환 후 다음 페이지 순회. */
	if (is_kern_pte (pte))
		return true;

	/* 2. 부모의 페이지 맵 레벨 4에서 VA를 찾는다. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
		return false;
	}

	/* 3. TODO: 자식용 새 PAL_USER 페이지를 할당하고 결과를
	 *    TODO: NEWPAGE에 저장한다. */
	newpage = palloc_get_page (PAL_USER);
	if (newpage == NULL) {
		return false;
	}

	/* 4. TODO: 부모 페이지 내용을 새 페이지로 복사하고,
	 *    TODO: 부모 페이지의 쓰기 가능 여부를 확인해 그 결과에 따라
	 *    TODO: WRITABLE을 설정한다. */
	memcpy (newpage, parent_page, PGSIZE);

	/* 5. VA 주소에 WRITABLE 권한으로 새 페이지를 자식의 페이지 테이블에
	 *    추가한다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: 페이지 삽입에 실패하면 오류 처리를 한다. */
		palloc_free_page (newpage);
		return false;
	}
	return true;
}
#endif

/* 부모의 실행 문맥을 복사하는 스레드 함수.
 * 힌트) parent->tf에는 프로세스의 사용자 영역 문맥이 들어 있지 않다.
 *       즉, process_fork의 두 번째 인자를 이 함수로 전달해야 한다. */
static void
__do_fork (void *aux_) {
	struct intr_frame if_;
	struct fork_aux *parent_aux = aux_;
	struct thread *current = thread_current ();
	struct thread *parent = parent_aux->parent;
	struct child_status *status = parent_aux->child_status;

	current->my_status = status;

	/* 1. CPU 문맥을 로컬 스택으로 읽어온다. */
	memcpy(&if_, &parent_aux->parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0;

	/* 2. 페이지 테이블을 복제한다. */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL) {
		goto error;
	}

	process_activate(current);

#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt)) {
		goto error;
	}
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)) {
		goto error;
	}
#endif

	current->next_fd = parent->next_fd;
	struct list_elem *e;
	for (e = list_begin(&parent->fd_list);
		 e != list_end(&parent->fd_list);
		 e = list_next(e)) {
		struct fd_entry *entry = list_entry(e, struct fd_entry, elem);
		struct fd_entry *child_entry = malloc(sizeof *child_entry);
		if (child_entry == NULL) {
			goto error;
		}
		struct file *file = file_duplicate(entry->file);
		if (file == NULL) {
			free(child_entry);
			goto error;
		}
		child_entry->fd = entry->fd;
		child_entry->file = file;
		list_push_back(&current->fd_list, &child_entry->elem);
	}

	if (parent->exec_file != NULL) {
		current->exec_file = file_duplicate(parent->exec_file);
		if (current->exec_file == NULL) {
			goto error;
		}
	}

	parent_aux->success = true;
	sema_up(&parent_aux->fork_sema);
	do_iret (&if_);
error:
	parent_aux->success = false;
	sema_up(&parent_aux->fork_sema);
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	struct thread *current = thread_current();
	uint64_t *old_pml4 = current->pml4;
	struct file *old_exec_file = current->exec_file;


	/* thread 구조체 안의 intr_frame은 사용할 수 없다.
	 * 현재 스레드가 다시 스케줄될 때 해당 멤버에 실행 정보가 저장되기
	 * 때문이다. */
	struct intr_frame _if; 
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* 
	 * 문맥 정리 후 적재하면 load 실패 시 데이터 손실 발생
	 * load 되는지 확인 후, 문맥 정리
	 * 
	 * 기존 pml4 저장
	 * load 시도
	 * 실패 시 기존 pml4 복구, 새로 만든 pml4 폐기
	 * 성공하면 기존 pml4 폐기, do_iret()
	*/

	/* 그다음 바이너리를 적재한다. */
	success = load (file_name, &_if);
	palloc_free_page (file_name);

	/* 적재에 실패하면 종료한다. */
	if (!success) {
		uint64_t *new_pml4 = current->pml4;
		current->pml4 = old_pml4;

		process_activate(current);

		if (new_pml4 != NULL) {
			pml4_destroy(new_pml4);
		}

		return -1;
	}

	if (old_exec_file != NULL) {
    	file_allow_write(old_exec_file);
    	file_close(old_exec_file);
	}

	if (old_pml4 != NULL) {
		pml4_destroy(old_pml4);
	}

	/* 전환된 프로세스를 시작한다. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* 스레드 TID가 종료될 때까지 기다렸다가 종료 상태를 반환한다. 커널에 의해
 * 종료된 경우(즉, 예외 때문에 죽은 경우)에는 -1을 반환한다. TID가
 * 잘못되었거나, 호출한 프로세스의 자식이 아니거나, 해당 TID에 대해
 * process_wait()가 이미 성공적으로 호출된 적이 있다면 기다리지 않고 즉시
 * -1을 반환한다.
 *
 * 이 함수는 problem 2-2에서 구현한다. 현재는 아무 일도 하지 않는다. */
int
process_wait (tid_t child_tid) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	struct child_status *child = NULL;

	struct list_elem *e;
	for (e = list_begin(child_list); e != list_end(child_list); e = list_next(e)) {
		struct child_status *tmp = list_entry(e, struct child_status, elem);
		if (tmp->tid == child_tid) {
			child = tmp;
			break;
		}
	}

	if (child == NULL) {
		return -1;
	}

	lock_acquire(&child->lock); 

	if (child->waited) {
		lock_release(&child->lock);
		return -1;
	}

	child->waited = true;
	bool exited = child->exited;
	lock_release(&child->lock);

	if (!exited) {
		sema_down(&child->wait_sema);
	}

	lock_acquire(&child->lock);
	int status = child->exit_status;
	lock_release(&child->lock);

	list_remove(&child->elem);
	child_status_release(child);
	return status;
}

/* 프로세스를 종료한다. 이 함수는 thread_exit()에서 호출된다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: 여기에 코드를 작성한다.
	 * TODO: 프로세스 종료 메시지를 구현한다
	 * TODO: (project2/process_termination.html 참고).
	 * TODO: 프로세스 자원 정리도 여기서 구현하는 것을 권장한다. */

	if (curr->pml4 != NULL) {
		printf ("%s: exit(%d)\n", thread_name(), (int) curr->exit_status);
	}
	
	struct child_status *child = curr->my_status;

	if (child != NULL) {
		lock_acquire(&child->lock);
		child->exit_status = curr->exit_status;
		child->exited = true;
		lock_release(&child->lock);

		sema_up(&child->wait_sema);
		child_status_release(child);
	}

	struct list_elem *e = list_begin(&curr->fd_list);

	while (e != list_end(&curr->fd_list)) {
		struct fd_entry *entry = list_entry(e, struct fd_entry, elem);
		e = list_remove(e);

		file_close(entry->file);
		free(entry);
	}

	struct list_elem *e2 = list_begin(&curr->child_list);

	while (e2 != list_end(&curr->child_list)) {
		struct child_status *child = list_entry(e2, struct child_status, elem);
		e2 = list_remove(e2);
		child_status_release(child);
	}

	if (curr->exec_file != NULL) {
		file_allow_write(curr->exec_file);
		file_close(curr->exec_file);
		curr->exec_file = NULL;
	}

	process_cleanup ();
}

static void
child_status_release (struct child_status *child) {
	bool do_free = false;

	if (child == NULL) {
		return;
	}

	lock_acquire(&child->lock);
	child->ref_cnt--;
	ASSERT(child->ref_cnt >= 0);
	do_free = (child->ref_cnt == 0);
	lock_release(&child->lock);

	if (do_free) {
		free(child);
	}

}

/* 현재 프로세스의 자원을 해제한다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 파괴하고 커널 전용 페이지
	 * 디렉터리로 돌아간다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서는 순서가 매우 중요하다. 타이머 인터럽트가 다시
		 * 프로세스 페이지 디렉터리로 전환하지 못하게, 페이지 디렉터리를
		 * 바꾸기 전에 cur->pagedir을 NULL로 설정해야 한다. 또한
		 * 프로세스 페이지 디렉터리를 파괴하기 전에 기본 페이지
		 * 디렉터리를 활성화해야 한다. 그렇지 않으면 현재 활성 페이지
		 * 디렉터리가 이미 해제된(그리고 비워진) 것이 된다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행할 수 있도록 CPU를 설정한다.
 * 이 함수는 문맥 전환이 일어날 때마다 호출된다. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화한다. */
	pml4_activate (next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정한다. */
	tss_update (next);
}

/* ELF 바이너리를 적재한다. 아래 정의는 [ELF1]의 ELF 명세를 거의 그대로
 * 가져온 것이다. */

/* ELF 타입. [ELF1] 1-2 참고. */
#define EI_NIDENT 16

#define PT_NULL    0            /* 무시. */
#define PT_LOAD    1            /* 적재 가능한 세그먼트. */
#define PT_DYNAMIC 2            /* 동적 링크 정보. */
#define PT_INTERP  3            /* 동적 로더 이름. */
#define PT_NOTE    4            /* 보조 정보. */
#define PT_SHLIB   5            /* 예약됨. */
#define PT_PHDR    6            /* 프로그램 헤더 테이블. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* 실행 가능. */
#define PF_W 2          /* 쓰기 가능. */
#define PF_R 4          /* 읽기 가능. */

/* 실행 파일 헤더. [ELF1] 1-4~1-8 참고.
 * ELF 바이너리의 맨 앞부분에 위치한다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* 약어 */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	/* file_name 파싱을 위한 copy 만들기 */
	char *file_name_copy = palloc_get_page(0);
	if (file_name_copy == NULL) {
		goto done;
	}
	strlcpy(file_name_copy, file_name, PGSIZE);
	/* NULL까지 반복하여 argv 만들기 */
	char *argv[64];
	char *save_ptr;
	char *token = strtok_r(file_name_copy, " ", &save_ptr);
	if (token == NULL) {
		goto done;
	}
	int argc = 0;
	while (token != NULL && argc < 63 ) {
		argv[argc] = token;
		argc++;
		token = strtok_r(NULL, " ", &save_ptr);
	}
	argv[argc] = NULL; // 배열의 마지막 값은 NULL로 설정

	/* 페이지 디렉터리를 할당하고 활성화한다. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	file_deny_write(file);

	/* 실행 파일 헤더를 읽고 검증한다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64 아키텍처
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더를 읽는다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시한다. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반 세그먼트.
						 * 앞부분은 디스크에서 읽고 나머지는 0으로 채운다. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* 전부 0으로 채워진 세그먼트.
						 * 디스크에서는 아무것도 읽지 않는다. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택을 설정한다. */
	if (!setup_stack (if_))
		goto done;

	/* 시작 주소를 설정한다. */
	if_->rip = ehdr.e_entry;

	/* Implement argument passing (see project2/argument_passing.html). */
	/* 스택에 토큰 올리기 */
	// 스택 맨 위에 명령줄 문자열 삽입한다
	char *arg_addr[64];
	int j = 0;

	while (argv[j] != NULL) {
		size_t str_len = strlen(argv[j]) + 1;


		if_->rsp -= str_len;
		memcpy((void *)if_->rsp, argv[j], str_len);

		arg_addr[j] = (char *)if_->rsp;
		j++;
	}

	// 스택 주소 시작점을 word 크기로 정렬한다
	while (if_->rsp % 8 != 0) {
		if_->rsp--;
		*(uint8_t *)if_->rsp = 0;
	}

	// 맨 처음 NULL 삽입
	if_->rsp -= 8;
	char *null_ptr = NULL;
	memcpy((void *)if_->rsp, &null_ptr, sizeof(null_ptr));


	// rsp를 늘리고 agrv 역순으로 삽입 반복 until argc == 0
	while (j > 0) {
		j--;
		if_->rsp -= 8;
		memcpy((void *)if_->rsp, &arg_addr[j], sizeof(arg_addr[j]));
	}

	// argv 시작 주소
	char **argv_addr = (char **) if_->rsp;

	// 마지막에 가짜 return 주소 삽입
	char *fake_rex = 0;
	if_->rsp -= sizeof(fake_rex);
	memcpy((void *)if_->rsp, &fake_rex, sizeof(fake_rex));

	if_->R.rdi = argc;
	if_->R.rsi = (uint64_t)argv_addr;
	success = true;

	t->exec_file = file;
	file = NULL;

done:
	/* We arrive here whether the load is successful or not. */
	if (!success && file != NULL) {
		file_close (file);
	}
	if (file_name_copy != NULL) {
		palloc_free_page(file_name_copy);
	}
	return success;
}


/* PHDR이 FILE 안의 유효하고 적재 가능한 세그먼트를 설명하는지 검사한다.
 * 맞으면 true, 아니면 false를 반환한다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr은 같은 페이지 오프셋을 가져야 한다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 내부를 가리켜야 한다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 최소한 p_filesz 이상이어야 한다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어 있으면 안 된다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역의 시작과 끝은 모두 사용자 주소 공간 범위 안에
	   있어야 한다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 이 영역이 커널 가상 주소 공간을 가로질러 "되감기"처럼
	   넘쳐서는 안 된다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 0번 페이지 매핑은 허용하지 않는다.
	   0번 페이지를 매핑하는 것 자체도 좋지 않지만, 이를 허용하면 시스템
	   호출에 null 포인터를 넘긴 사용자 코드 때문에 memcpy() 등의 null
	   포인터 검사를 통해 커널이 패닉할 가능성이 크다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 문제가 없다. */
	return true;
}

#ifndef VM
/* 이 블록의 코드는 project 2에서만 사용된다.
 * project 2 전체에 대해 함수를 구현하고 싶다면 #ifndef 바깥에 구현하라. */

/* load() 보조 함수들. */
static bool install_page (void *upage, void *kpage, bool writable);

/* FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 적재한다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이
 * 초기화한다:
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE의 OFS부터 읽어야 한다.
 *
 * - UPAGE + READ_BYTES 이후의 ZERO_BYTES 바이트는 0으로 채워야 한다.
 *
 * WRITABLE이 true이면 여기서 초기화한 페이지는 사용자 프로세스가 쓸 수
 * 있어야 하고, 아니면 읽기 전용이어야 한다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 나면
 * false를 반환한다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채운다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리 페이지 하나를 가져온다. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* 이 페이지를 적재한다. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 이 페이지를 프로세스 주소 공간에 추가한다. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* 다음 페이지로 진행한다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK 위치에 0으로 채운 페이지를 매핑해 최소한의 스택을 만든다. */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 사용자 가상 주소 UPAGE를 커널 가상 주소 KPAGE에 연결하는 매핑을
 * 페이지 테이블에 추가한다.
 * WRITABLE이 true이면 사용자 프로세스가 이 페이지를 수정할 수 있고,
 * 아니면 읽기 전용이다.
 * UPAGE는 아직 매핑되어 있지 않아야 한다.
 * KPAGE는 보통 palloc_get_page()로 사용자 풀에서 얻은 페이지여야 한다.
 * 성공하면 true를 반환하고, UPAGE가 이미 매핑되어 있거나 메모리 할당에
 * 실패하면 false를 반환한다. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* 해당 가상 주소에 페이지가 이미 없는지 확인한 뒤, 거기에 페이지를
	 * 매핑한다. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* 여기부터의 코드는 project 3 이후에 사용된다.
 * project 2만 대상으로 구현하려면 위쪽 블록에 구현하라. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: 파일에서 세그먼트를 적재한다. */
	/* TODO: 이 함수는 VA 주소에서 첫 페이지 폴트가 발생했을 때 호출된다. */
	/* TODO: VA는 이 함수가 호출될 때 사용할 수 있다. */
}

/* FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 적재한다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이
 * 초기화한다:
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE의 OFS부터 읽어야 한다.
 *
 * - UPAGE + READ_BYTES 이후의 ZERO_BYTES 바이트는 0으로 채워야 한다.
 *
 * WRITABLE이 true이면 여기서 초기화한 페이지는 사용자 프로세스가 쓸 수
 * 있어야 하고, 아니면 읽기 전용이어야 한다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 나면
 * false를 반환한다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채운다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: lazy_load_segment에 전달할 정보를 담은 aux를 준비한다. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* 다음 페이지로 진행한다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 스택 페이지를 만든다. 성공하면 true를 반환한다. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 점유한다.
	 * TODO: 성공하면 rsp를 그에 맞게 설정한다.
	 * TODO: 해당 페이지를 스택 페이지로 표시해야 한다. */
	/* TODO: 여기에 코드를 작성한다. */

	return success;
}
#endif /* VM */
