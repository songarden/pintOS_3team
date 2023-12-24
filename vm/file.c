/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/userprog/process.h"

static bool
load_file_page (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable);

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
	struct load_info *load_info = &page->uninit.aux;
	struct file_page *file_page = &page->file;
	file_page->file_info = load_info;
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
	struct thread *curr = thread_current();
	struct file_page *file_page UNUSED = &page->file;
	struct load_info *file_info = file_page->file_info;
	if(pml4_is_dirty(curr->pml4,page->va)){
		lock_acquire(&filesys_lock);
		file_write_at(file_info->file,page->frame->kva,file_info->page_read_bytes,file_info->ofs);
		lock_release(&filesys_lock);
		pml4_set_dirty(curr->pml4,page->va,0);
	}
	free(file_info);
	free(page->frame);
	pml4_clear_page(curr->pml4,page->va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct thread *curr = thread_current();
	if(addr != pg_round_down(addr)){
		printf("not aligned addr\n");
		return NULL;
	}
	if(addr == NULL){
		printf("NULL point denied\n");
		return NULL;
	}
	if(addr <= USER_STACK && addr >= curr->stack_bottom){
		printf("This is stack area\n");
		return NULL;
	}

	void *check_addr = addr;

	do{
		if(spt_find_page(&curr->spt,check_addr) == NULL){
			check_addr += PGSIZE;
		}
		else{
			printf("파일 맵 할 블록 중 곂치는 페이지가 있음\n");
			return NULL;
		}
	}while(check_addr > addr + length);
	size_t read_bytes = offset + length;
	size_t zero_bytes = (ROUND_UP (offset + length, PGSIZE)- read_bytes);
	if(!load_file_page(file,offset,addr,read_bytes,zero_bytes,writable)){
		return addr;
	}
	else{
		return NULL;
	}
}

static bool
load_file_page (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct load_info *load_info = (struct load_info *)malloc(sizeof(struct load_info));
		if(load_info == NULL){
			return false;
		}
		load_info->file = file;
		load_info->page_read_bytes = page_read_bytes;
		load_info->page_zero_bytes = page_zero_bytes;
		load_info->ofs = ofs;
		if(!vm_alloc_page_with_initializer(VM_FILE,upage,writable,lazy_load_segment,load_info)){
			free(load_info);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

bool
lazy_load_file_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct load_info *load_info = (struct load_info *)aux;
	uint8_t *kpage = page->frame->kva;
	struct file *file = load_info->file;
	size_t page_read_bytes = load_info->page_read_bytes;
	size_t page_zero_bytes = load_info->page_zero_bytes;
	off_t ofs = load_info->ofs;
	
	file_seek(file,ofs);
	/* Load this page. */
	if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
		palloc_free_page (kpage);
		free(load_info);
		return false;
	}
	memset (kpage + page_read_bytes, 0, page_zero_bytes);

	return true; 
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt,addr);
	if(page == NULL){
		return;
	}
	if(page->operations->type != VM_FILE){
		return;
	}
	spt_remove_page(&curr->spt,page);
}
