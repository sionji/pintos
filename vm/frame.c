#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"

struct list_elem *lru_clock;
void* try_to_free_pages (enum palloc_flags flags);
void __free_page (struct page *page);
static struct list_elem *get_next_lru_clock (void);

struct page *
alloc_page (enum palloc_flags flags)
{
  /* If palloc_get_page is failed, try to free pages. */
  void *kaddr = palloc_get_page (flags);
  if (kaddr == NULL)
    kaddr = try_to_free_pages (flags);
  
  /* Page & Memory allocation. */
  struct page *page = (struct page *)malloc (sizeof (struct page));
  if (page == NULL)
  {
    palloc_free_page (kaddr);
    return NULL;
  }

  /* Initialize. */
  page->kaddr = kaddr;
  page->vme = NULL;
  page->thread = thread_current ();

  /* Insertion. */
  lock_acquire (&lru_list_lock);
  add_page_to_lru_list (page);
  lock_release (&lru_list_lock);

  return page;
}

void
lru_list_init (void)
{
  list_init (&lru_list);
  lock_init (&lru_list_lock);
  /* Set lru_clock value to NULL. */
  /* !ERROR CODE! 
     lru_list is empty list now, so there is no element in list. 
     Then lru_clock may indicate NULL, 
     so try_to_free_pages cannot make it's entry.*/
  //lru_clock = list_begin (&lru_list);
  lru_clock = NULL;
}

void 
add_page_to_lru_list (struct page *page)
{
  list_push_back (&lru_list, &page->lru);
}

void 
del_page_to_lru_list (struct page *page)
{
  list_remove (&page->lru);
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

/* No space left, try to free pages and allocate new frame. */
void * 
try_to_free_pages (enum palloc_flags flags)
{
  void *kaddr = NULL;
  struct page *page;
  struct vm_entry *vme;

  while (kaddr == NULL)
  {
    /* If lru_clock indicates NULL, then change it to begin of lru_list. */
    if (lru_clock == NULL)
      lru_clock = list_begin (&lru_list);

    /* Get page and vm_entry. */
    page = list_entry (lru_clock, struct page, lru);
    vme = page->vme;

    /* You must move lru_clock becasue selected page may be free. */
    lru_clock = get_next_lru_clock ();

    lock_acquire (&lru_list_lock);

    /* Check pagedir_is_accessed. */
    if (pagedir_is_accessed (page->thread->pagedir, page->vme->vaddr))
      pagedir_set_accessed (page->thread->pagedir, page->vme->vaddr, false);

    /* Victim eviction. */
    switch (page->vme->type)
    {
      case VM_BIN :
        {
          page->vme->type = VM_ANON;
          page->vme->swap_slot = swap_out (page->kaddr);
          break;
        }

      case VM_FILE :
        {
          /* Check pagedir_is_dirty. */
          if (pagedir_is_dirty (page->thread->pagedir, page->vme->vaddr))
          {
            /* Lock must be used at every read/write operation. 
               Assume that if lock is acquired by other thread,
               then status of current thread will be changed to THREAD_BLOCK. 
               If you don't use lock, it will make script confused. 
               (lock_held_by_current_thread () script error will cause.)  */
            lock_acquire (&filesys_lock);
            file_write_at (vme->file, vme->vaddr, vme->read_bytes, vme->offset);
            pagedir_set_dirty (page->thread->pagedir, vme->vaddr, false);
            lock_release (&filesys_lock);
          } 
          /* Do not swap it out. Just write it and free. */
          //page->vme->type = VM_ANON;
          //page->vme->swap_slot = swap_out (page->kaddr);
          break;
        }
  
      case VM_ANON :
        {
          /* Always write at swap partition. */
          page->vme->swap_slot = swap_out (page->kaddr);
          break;
        }
    }

    /* Free page. */
    page->vme->is_loaded = false;
    __free_page (page);

    /* Memory allocation and return it's pointer.*/
    kaddr = palloc_get_page (flags);

    lock_release (&lru_list_lock);
  }

  return kaddr;
}

void
__free_page (struct page *page)
{
  /* List remove. */
  del_page_to_lru_list (page);
  /* Free Physical page. */
  palloc_free_page (page->kaddr);
  /* Clear virtual page. */
  pagedir_clear_page (page->thread->pagedir, page->vme->vaddr);
  /* Free struct page. */
  free (page);

  /* Do not free vme because vm_destroy will be called before exit PintOS. */
}

void 
free_page (void *kaddr)
{
  struct list_elem *e, *tmp;
  lock_acquire (&lru_list_lock);
  for (e = list_begin (&lru_list); e != list_end (&lru_list);
       e = tmp)
  {
    struct page *page = list_entry (e, struct page, lru);
    /* Selected list_elem will be removed from lru_list.
       So just change list_elem before free. */
    tmp = list_next (e);
    if (page->kaddr == kaddr)
      __free_page (page);
  }
  lock_release (&lru_list_lock);
}

