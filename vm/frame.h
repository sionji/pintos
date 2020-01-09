#ifndef VM_FRAME_H_
#define VM_FRAME_H_
	
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

struct list lru_list;
struct lock lru_list_lock;

void lru_list_init (void);
struct page *alloc_page (enum palloc_flags flags);
void add_page_to_lru_list (struct page *);
void del_page_to_lru_list (struct page *);
static struct list_elem *get_next_lru_clock (void);
void free_page (void *kaddr);

#endif
