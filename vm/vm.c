/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "include/lib/stdio.h"
#include "devices/timer.h"





bool
page_less (const struct hash_elem *a_,const struct hash_elem *b_, void *aux UNUSED);

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	list_init(&frame_list);
	sema_init(&swap_sema,1);
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
static struct page *clock_evict_policy(void);
static bool vm_do_claim_page (struct page *page);
static bool
vm_connect_page_frame(struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	bool (*page_initializer)(struct page*, enum vm_type,void*);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		
		if(VM_TYPE(type) == VM_ANON){
			page_initializer = anon_initializer;
			uninit_new(new_page,upage,init,type,aux,page_initializer);
		}
		else if(VM_TYPE(type) == VM_FILE){
			page_initializer = file_backed_initializer;
			uninit_new(new_page,upage,init,type,aux,page_initializer);
		}
		else{
			printf("page 유형이 올바르지 않습니다.");
			goto err;
		}
		/* TODO: Insert the page into the spt. */
		new_page->writable = writable;
		return spt_insert_page(spt,new_page);
	}
	else {
		printf("이미 spt에 생성하려는 페이지가 있습니다.");
		goto err;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	page = malloc(sizeof(struct page));
	struct hash_elem *elem;
	
	page -> va = pg_round_down(va);
	elem = hash_find(&spt->pages,&page->spt_elem);
	if(elem == NULL){
		return NULL;
	}
	free(page);
	return hash_entry(elem,struct page,spt_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	// struct hash_elem *hash_elem;
	/* TODO: Fill this function. */
	if((hash_insert(&spt->pages,&page->spt_elem)) == NULL){
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
	struct page *victim_page = clock_evict_policy();
	struct frame *victim = victim_page->frame;
	 /* TODO: The policy for eviction is up to you. */
	
	return victim;
}

static struct page *clock_evict_policy (void) {
	struct list_elem *elem;
	struct list_elem *start_elem = clock_buffer_elem;
	struct page *evict_page = NULL;
	do{
		for(elem = start_elem->next;elem != list_end(&frame_list);list_next(elem)){
			evict_page = list_entry(elem,struct page,frame_elem);
			if(pml4_is_accessed(evict_page->thread->pml4,evict_page->va)){
				pml4_set_accessed(evict_page->thread->pml4,evict_page->va,false);
			}
			else{
				clock_buffer_elem = elem != list_begin(&frame_list) ? elem->prev:list_end(&frame_list)->prev;
				return evict_page;
			}
		}
		start_elem = list_head(&frame_list);
	}while(true);
	
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	struct page *victim_page = victim->page;
	void *kva = victim->kva;
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim_page);
	return kva;
}

/* palloc() and get frame. If there is no available page, evict(내쫓다) the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	frame = (struct frame *)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER|PAL_ZERO);
	frame->page = NULL;
	if(frame->kva == NULL){
		frame->kva = vm_evict_frame();
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	struct thread *curr = thread_current();
	void *new_stack_bottom = curr->stack_bottom;
	new_stack_bottom -= PGSIZE;
	if (vm_alloc_page (VM_ANON|VM_MARKER_0,new_stack_bottom,true)){
		bool success = vm_claim_page(new_stack_bottom);
		if(success){
			curr->stack_bottom = (void *)new_stack_bottom;
		}
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
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	struct page *page = NULL;

	if(!not_present){
		return false;
	}
	if(user){
		if(!is_user_vaddr(addr) || addr == NULL){
			return false;
		}
	}
	
	page = spt_find_page(spt,addr);
	if(page == NULL){
		if((user && addr >= f->rsp-8 )||(!user && addr >= curr->curr_rsp-8 )){
			if(curr->stack_bottom >= USER_STACK - stack_growth_limit+PGSIZE && addr <= USER_STACK){
				vm_stack_growth(addr);
				return true;
			}
			else{
				return false;
			}
		}
		else{
			return false;
		}
	}
	return vm_do_claim_page (page);
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
	struct page *page = spt_find_page(&thread_current()->spt,va);
	/* TODO: Fill this function */

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	sema_down(&swap_sema);
	if(!vm_connect_page_frame(page)){
		return false;
	}
	struct frame *frame =page->frame;
	bool success = swap_in (page, frame->kva);
	sema_up(&swap_sema);
	return success;
}

static bool
vm_connect_page_frame(struct page *page){
	struct frame *frame = vm_get_frame ();
	struct thread *curr = thread_current ();
	if(frame == NULL){
		exit(-4);
	}
	/* Set links */
	frame->page = page;
	page->frame = frame;
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!(pml4_get_page(curr->pml4, page->va) == NULL
			&& pml4_set_page (curr->pml4, page->va, frame->kva, page->writable))){
		palloc_free_page(frame->kva);
		return false;
	}
	return true;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	struct thread *curr = thread_current();
	if (!hash_init(&spt->pages,page_hash,page_less,NULL)){
		printf("빡종");
		exit(-1);
	}
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	bool success = true;
	struct hash_iterator i;
	
   	hash_first (&i, &src->pages);
   	while (hash_next (&i)){
		struct page *cp_page = hash_entry (hash_cur (&i), struct page, spt_elem);
		enum vm_type cp_type = cp_page->operations->type;
		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		if(new_page == NULL){
			exit(-13);
		}
		memcpy(new_page,cp_page,sizeof(struct page));
		new_page->frame = NULL;
		new_page->thread = thread_current();
		spt_insert_page(dst,new_page);
		switch(VM_TYPE(cp_type)){
			case VM_UNINIT:
				success = uninit_duplicate_aux(cp_page,new_page);
				if(!success){
					return false;
				}
				break;
			case VM_ANON:
				if(!(cp_type & VM_SWAP)){
					if(!vm_connect_page_frame(new_page)){
						return false;
					}
					memcpy(new_page->frame->kva,cp_page->frame->kva,PGSIZE);
				}
				new_page->anon.fork_cnt += 1;
				break;
			case VM_FILE:
				if(!(cp_type & VM_DISK)){
					if(!vm_connect_page_frame(new_page)){
						return false;
					}
					memcpy(new_page->frame->kva,cp_page->frame->kva,PGSIZE);
				}
				struct file *reopen_file = file_reopen(&cp_page->file.file);
				struct file_page *file_page = &new_page->file;
				file_page->file = reopen_file;
				break;
		}
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->pages,hash_action_free);
}

void hash_action_free (struct hash_elem *e,void *aux){
	struct page *page = hash_entry(e,struct page,spt_elem);
	sema_down(&swap_sema);
	vm_dealloc_page(page);
	sema_up(&swap_sema);
}


/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, spt_elem);
  return hash_bytes (&p->va, sizeof p->va);
}


bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, spt_elem);
  const struct page *b = hash_entry (b_, struct page, spt_elem);

  return a->va < b->va;
}

void vm_remove_frame(struct page *page){
	if(clock_buffer_elem == &page->frame_elem){
		if(list_begin(&frame_list) == &page->frame_elem){
			clock_buffer_elem = list_end(&frame_list)->prev;
		}
		else{
			clock_buffer_elem = clock_buffer_elem->prev;
		}
	}
	list_remove(&page->frame_elem);
}

