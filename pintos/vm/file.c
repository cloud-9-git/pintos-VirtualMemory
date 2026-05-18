/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool lazy_load_file (struct page *page, void *aux);
static bool load_file (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable);

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

	struct file_aux *aux_ = page->uninit.aux; 

	page->file = (struct file_page) {
		.writable = aux_->writable, 
		.page_read_bytes = aux_->page_read_bytes, 
		.offset = aux_->offset,
		.file = aux_->file 
	};

	// return uninit->page_initializer (page, uninit->type, kva) &&
	// (init ? init (page, aux) : true);
	
	// TODO: 이 코드 왜 있는지 찾아보고 false인 경우가 언제인지 찾아보기 
	// struct file_page *file_page = &page->file;
	return true; 
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	return true; 
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

static bool
lazy_load_file (struct page *page, void *aux) {
	struct file_aux *aux_ = (struct file_aux *) aux;

	if (file_read_at (aux_->file, page->frame->kva, aux_->page_read_bytes, aux_->offset) != (int) aux_->page_read_bytes) {		
		palloc_free_page (page->frame->kva);		
		return false;
	}
	memset (page->frame->kva + aux_->page_read_bytes, 0, aux_->page_zero_bytes);
	return true; 
}

static bool
load_file (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	// ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// ASSERT (pg_ofs (upage) == 0);
	// ASSERT (ofs % PGSIZE == 0);
	while (read_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채운다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
	
		void *aux = NULL;
		struct file_aux *aux_ = malloc(sizeof(struct file_aux));

		if (aux_ == NULL) {
			// TODO: heap영역도 부족하면 swap out 해야하나?
			return false;
		}
		aux_->file = file;
		aux_->page_read_bytes = page_read_bytes;
		aux_->page_zero_bytes = page_zero_bytes;
		aux_->offset = ofs;
		aux_->writable = writable;

		aux = aux_;
	
		if (!vm_alloc_page_with_initializer (VM_FILE, upage,
					writable, lazy_load_file, aux)) {	
	
			free(aux_);		
			return false;			
		}

		/* 다음 페이지로 진행한다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		ofs += PGSIZE;
		upage += PGSIZE;
	}

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	/*
		length bytes를 fd로 열린 파일에서 offset byte부터 프로세스(process)의 가상 주소 공간 addr에 매핑합니다.
		전체 파일은 addr에서 시작하는 연속적인 가상 페이지에 매핑됩니다.
		파일 길이가 PGSIZE의 배수가 아니면 마지막으로 매핑된 페이지의 일부 bytes가 파일 끝을 넘어 "삐져나옵니다".
		이 페이지에서 페이지 폴트(page fault)가 발생해 메모리로 읽어 들일 때 해당 bytes를 0으로 설정하고, 페이지를 디스크에 다시 쓸 때는 버립니다.
		성공하면 이 함수는 파일이 매핑된 가상 주소를 반환합니다.
		실패하면 파일 매핑에 유효한 주소가 아닌 NULL을 반환해야 합니다.
	*/

	/* # validation */
	// # 읽을 크기가 0 이하이면 실패
	if (length <= 0) {
		return NULL;
	}

	if (addr == 0) {
		return NULL;
	}

	if (addr != pg_round_down(addr)) {
		return NULL; 
	}

	if (file == NULL) {
		return NULL; 
	}

	// # TODO: fd에서 열린 파일의 크기가 0이면 실패
	if (file_length (file) <= 0) {
		return NULL;
	}
	
	/* # mapping */
	// # TODO: SPT에 넣기
	// size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
	size_t file_size = length < file_length(file) ? length : file_length(file); 

	load_file (file, offset, addr, file_size, 0, writable);

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	
}
