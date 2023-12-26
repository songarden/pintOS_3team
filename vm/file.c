/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/userprog/process.h"
#include "include/lib/stdio.h"
#include "vm/file.h"

static bool
load_file_page (struct file *file_origin, off_t ofs, uint8_t *upage,
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
	file_page->type = type;
	return true;
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
	struct file_page *file_page = &page->file;
	if(pml4_is_dirty(curr->pml4,page->va) && file_page->page_read_bytes > 0){
		lock_acquire(&filesys_lock);
		file_write_at(file_page->file,page->frame->kva,file_page->page_read_bytes,file_page->ofs);
		lock_release(&filesys_lock);
		pml4_set_dirty(curr->pml4,page->va,0);
	}
	file_close(file_page->file);
	palloc_free_page(page->frame->kva);
	pml4_clear_page(curr->pml4,page->va);
	free(page->frame);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct thread *curr = thread_current();
	
	if(addr != pg_round_down(addr)){
		return NULL;
	}
	if(addr == NULL){
		printf("NULL point denied\n");
		return NULL;
	}
	if(addr <= USER_STACK && addr >= curr->stack_bottom){
		return NULL;
	}

	void *check_addr = addr;

	do{
		if(spt_find_page(&curr->spt,check_addr) == NULL){
			check_addr += PGSIZE;
		}
		else{
			return NULL;
		}
	}while(check_addr < addr + length);

	size_t read_bytes = length>(file_length(file)-offset)?file_length(file)-offset:length;
	size_t zero_bytes = length>(file_length(file)-offset)?length-(file_length(file)-offset):ROUND_UP(read_bytes,PGSIZE)-read_bytes;

	if(load_file_page(file,offset,addr,read_bytes,zero_bytes,writable)){
		return addr;
	}
	else{
		return NULL;
	}
}

static bool
load_file_page (struct file *file_origin, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);
	void *start_upage = upage;
	// int i=0;
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		struct file *file = file_reopen(file_origin);
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct load_info *load_info = (struct load_info *)malloc(sizeof(struct load_info));
		if(load_info == NULL){
			file_close(file);
			return false;
		}
		load_info->file = file;
		load_info->page_read_bytes = page_read_bytes;
		load_info->page_zero_bytes = page_zero_bytes;
		load_info->ofs = ofs;
		//mmap 요청 시작 주소의 페이지에 마킹해두기
		if(start_upage == upage){
			if(!vm_alloc_page_with_initializer(VM_FILE|VM_MARKER_1,upage,writable,lazy_load_file_segment,load_info)){
				file_close(file);
				free(load_info);
				return false;
			}
		}
		else{
			if(!vm_alloc_page_with_initializer(VM_FILE,upage,writable,lazy_load_file_segment,load_info)){
				file_close(file);
				free(load_info);
				return false;
			}
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
	if(page_read_bytes > 0){
		if (file_read_at (file, kpage, page_read_bytes,ofs) != (int) page_read_bytes) {
		palloc_free_page (kpage);
		free(load_info);
		return false;
	}
	}
	/* Load this page. */
	memset (kpage + page_read_bytes, 0, page_zero_bytes);
	pml4_set_dirty(thread_current()->pml4,page->va,0);
	struct file_page *file_page = &page->file;
	file_page->file = file;
	file_page->page_read_bytes = page_read_bytes;
	file_page->page_zero_bytes = page_zero_bytes;
	file_page->ofs = ofs;
	free(load_info);

	return true; 
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// int i = 0;
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt,addr);
	if(page == NULL){
		return;
	}
	if(page_get_type(page) != VM_FILE){
		return;
	}
	if(page->operations->type == VM_UNINIT && page->uninit.type != (VM_FILE|VM_MARKER_1)){
		return;
	}
	if(page->file.type != (VM_FILE|VM_MARKER_1)){
		return;
	}
	do{
		struct hash_elem *deleting_hash = &page->spt_elem;
		if(hash_delete(&curr->spt.pages,&page->spt_elem) != deleting_hash){
			return;
		}
		spt_remove_page(&curr->spt,page);
		addr += PGSIZE;
		page = spt_find_page(&curr->spt,addr);
		if(page == NULL){
			return;
		}
	}while((page->operations->type == VM_FILE && page->file.type == VM_FILE) || (page->operations->type == VM_UNINIT && page->uninit.type == VM_FILE));
}
