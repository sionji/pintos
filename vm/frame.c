#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"

struct list_elem *lru_clock;

struct page *
alloc_page (enum palloc_flags flags)
{
	/* Page & Memory allocation. */
	struct page *page = (struct page *)malloc (sizeof (struct page));
	if (page == NULL)
		return NULL;
	void *kaddr = palloc_get_page (flags);

	/* Initialize. */
	page->kaddr = kaddr;
	page->vme = NULL;
	page->thread = thread_current ();

	/* Insertion. */
	add_page_to_lru_list (page);

	return page;
}

void
lru_list_init (void)
{
	list_init (&lru_list);
	lock_init (&lru_list_lock);
	/* Set lru_clock value to NULL. */
	lru_clock = NULL;
}

void 
add_page_to_lru_list (struct page *page)
{
	list_push_back (&lru_list, page->lru);
}

void 
del_page_to_lru_list (struct page *page)
{
	list_pop_back (&lru_list);
}

static struct list_elem *
get_next_lru_clock (void)
{
	lru_clock = list_next (lru_clock);

	if (lru_clock == list_end (&lru_list))
		return NULL;
	else
		return lru_clock;
}

void 
try_to_free_pages (enum palloc_flags flags)
{
}

void
__free_page (struct page *page)
{
	del_page_to_lru_list (page);
	palloc_free_page (page->kaddr);
	free (page);
}

void 
free_page (void *kaddr)
{
	struct list_elem *e;
	for (e = list_begin (&lru_list); e != list_end (&lru_list);
			 e = list_next (e))
	{
		struct page *page = list_entry (e, struct page, lru);
		if (page->kaddr == kaddr)
			__free_page (page);
	}
}

