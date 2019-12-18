#include "vm/page.h"
#include "lib/kernel/hash.h"
	
static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, 
		const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func (struct hash_elem *e, void *aux UNUSED);

void 
vm_init (struct hash *vm)
{
	hash_init (vm, vm_hash_func, vm_less_func, 0); 
	return;
}

static unsigned
vm_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  return hash_int ((int)vme->vaddr);
}

static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED)
{
	struct vm_entry vm1, vm2;
	vm1 = hash_entry (a, struct vm_entry, elem);
	vm2 = hash_entry (b, struct vm_entry, elem);

	return ((int)vm1->vaddr < (int)vm2->vaddr ? true : false);
}

bool
insert_vme (struct hash *vm, struct vm_entry *vme)
{
	struct hash_elem *e = hash_insert (vm, &vme->elem);
	return (e == NULL ? true : false);
}

bool
delete_vme (struct hash *vm, struct vm_entry *vme)
{
	struct hash_elem *e = hash_delete (vm, &vme->elem);
	return (e == NULL ? false : true);
}

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

void
vm_destroy (struct hash *vm)
{
	hash_destroy (vm, vm_destroy_func);
}

static void 
vm_destroy_func (struct hash_elem *e, void *aux UNUSED);
{
	struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
	if (vme->is_loaded)
	{
		free_page (pagedir_get_page (thread_current ()->pagedir, vme->vaddr));
		pagedir_clear_page (thread_current ()->pagedir, vme->vaddr);
	}
	free (vme);
}
