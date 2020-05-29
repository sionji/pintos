#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/gdt.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "lib/kernel/hash.h"
#include <string.h>
  
static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, 
    const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func (struct hash_elem *e, void *aux UNUSED);

/* Initialize virtual memory. 
   All of the vm_entry are managed by hash table. */
void 
vm_init (struct hash *vm)
{
  hash_init (vm, vm_hash_func, vm_less_func, 0); 
  return;
}

/* Required hash function for hash structure. */
static unsigned
vm_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  return hash_int ((int)vme->vaddr);
}

/* Required hash function for hash structure. 
   In virtual memory project, we use virtual address to determine return value.*/
static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED)
{
  struct vm_entry *vm1, *vm2;
  vm1 = hash_entry (a, struct vm_entry, elem);
  vm2 = hash_entry (b, struct vm_entry, elem);

  return ((int)vm1->vaddr < (int)vm2->vaddr ? true : false);
}

/* Insert vm_entry to hash table. Return true if it is successful. */
bool
insert_vme (struct hash *vm, struct vm_entry *vme)
{
  struct hash_elem *e = hash_insert (vm, &vme->elem);
  return (e == NULL ? true : false);
}

/* Delete vm_entry from hash table. Return true if it is successful. */
bool
delete_vme (struct hash *vm, struct vm_entry *vme)
{
  struct hash_elem *e = hash_delete (vm, &vme->elem);
  return (e == NULL ? false : true);
}

/* Find vm_entry from hash table by using virtual address. */
struct vm_entry *
find_vme (void *vaddr)
{
  struct vm_entry vme;
  struct hash_elem *e;
  vme.vaddr = pg_round_down (vaddr);
  e = hash_find (&thread_current ()->vm, &vme.elem);
  if (e == NULL)
    return NULL;
  return hash_entry (e, struct vm_entry, elem);
}

/* Destroy given hash table. This function is called by process_exit().*/
void
vm_destroy (struct hash *vm)
{
  hash_destroy (vm, vm_destroy_func);
}

/* Required hash function to destroy hash table. 
   DESTRUCTOR may deallocate the memory used by the hash element. */
static void 
vm_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  if (vme->is_loaded)
  {
    /* Change palloc_get_page () to free_page (). */
    free_page (pagedir_get_page (thread_current ()->pagedir, vme->vaddr));
    pagedir_clear_page (thread_current ()->pagedir, vme->vaddr);
  }
  free (vme);
}

/* Check buffer is valid. */
/* to_write ? Only check_address : Check vme->writable too. */
void
check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write)
{
  void *cur_buffer = buffer;
  int i = 0;
  for (i = 0; i < size; i++)
  {
    struct vm_entry *vme = check_address (cur_buffer, esp);
    if (vme != NULL && to_write && vme->writable == false)
      syscall_exit (-1);

    cur_buffer++;
  }
}

/* Check the string pointer is valid. */
void
check_valid_string (const void *str, void *esp)
{
  char *string = (char *)str;
  for (; *string != 0; string += 1)
  {
    check_address (str, esp);
  }
}

/* Called by handle_mm_fault(). 
   Load page to physical memory from disk. */
bool
load_file (void *kaddr, struct vm_entry *vme)
{
  /* !ERROR CODE! Consider the case where vme is already loaded. 
     Then we don't need to read bytes again, never return false.
  if (vme->read_bytes <= 0)
    return false;      */
  

  if (vme->read_bytes > 0)
  {
    off_t actual_read = file_read_at (vme->file, kaddr, vme->read_bytes, vme->offset);
    if (actual_read != vme->read_bytes)
    {
      //delete_vme (&thread_current()->vm, vme);  /* Annotation or not? */
      return false;
    }
  }

  memset ((char *)kaddr + vme->read_bytes, 0, vme->zero_bytes);
  //printf ("load_file Result : true\n");   /* For debugging purpose. */
  return true;
}

