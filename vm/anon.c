/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_table;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	swap_table = bitmap_create(disk_size(swap_disk)/8);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	
	struct anon_page *anon_page = &page->anon;

	anon_page->type = type;
	anon_page->fork_cnt = 0;
	anon_page->swap_idx = -1;
	list_push_back(&frame_list,&page->frame_elem);
	clock_buffer_elem = &page->frame_elem;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	if(!(anon_page->type & VM_SWAP)){
		return false;
	}
	if(anon_page->swap_idx < 0){
		return false;
	}
	size_t swap_idx = anon_page->swap_idx;
	enum vm_type type = anon_page->type;
	page->frame->kva = kva;
	for(size_t i = swap_idx*8; i<swap_idx*8+8; i++){
		disk_read(swap_disk,i,kva);
		kva += 512;
	}
	anon_page->type = (type & ~VM_SWAP);
	anon_page->swap_idx = -1;
	if(anon_page->fork_cnt > 0){
		anon_page->fork_cnt--;
	}
	else{
		bitmap_reset(swap_table,swap_idx);
	}
	list_push_back(&frame_list,&page->frame_elem);
	clock_buffer_elem = &page->frame_elem;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if(anon_page->type & VM_SWAP){
		exit(-4);
		return false;
	}
	if(anon_page->swap_idx != -1){
		exit(-3);
		return false;
	}
	size_t swap_idx = bitmap_scan_and_flip(swap_table,0,1,false);
	if(swap_idx == BITMAP_ERROR){
		exit(-2);
		return false;
	}
	void *kva = page->frame->kva;
	enum vm_type type = anon_page->type;
	for(size_t i = swap_idx*8;i<swap_idx*8+8;i++){
		disk_write(swap_disk,i,kva);
		kva += 512;
	}
	anon_page->swap_idx = (int)swap_idx;

	page->frame->kva = NULL;
	pml4_clear_page(thread_current()->pml4,page->va);

	vm_remove_frame(page);
	anon_page->type = (VM_SWAP|type);
	
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if(anon_page->type & VM_SWAP){
		int swap_idx = anon_page->swap_idx;
		if(swap_idx < 0){
			exit(-12);
		}
		if(anon_page->fork_cnt > 0){
			anon_page->fork_cnt--;
		}
		else{
			bitmap_reset(swap_table,swap_idx);
		}
	}
	else{
		vm_remove_frame(page);
	}
	free(page->frame);
}
