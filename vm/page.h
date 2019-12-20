#ifndef VM_PAGE_H
#define VM_PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include "lib/kernel/hash.h"
#include "threads/vaddr.h"

struct vm_entry {
	uint8_t type;                     /* Type for VM_BIN, VM_FILE, VM_ANON. */
	void *vaddr;                      /* page number for vm_entry. */
	bool writable;                   /* True : writable, FALSE : un-writable. */

	bool is_loaded;                   /* Flag for physical memory load. */
	struct file* file;                /* Mapped file with virtual address. */

	/* For Memory mapped file */
	struct list_elem mmap_elem;       /* mmap list element */

	size_t offset;                    /* Offset which needs read. */
	size_t read_bytes;                /* Data size which is written in virtual page. */
	size_t zero_bytes;                /* Number of bytes which will be filled with zero. */

	/* For Swapping */
	size_t swap_slot;                 /* Swap slot. */

	/* Data structure for vm_entry */
	struct hash_elem elem;            /* Hash table element. */
};

void vm_init (struct hash *vm);
bool insert_vme (struct hash *, struct vm_entry *);
bool delete_vme (struct hash *, struct vm_entry *);
struct vm_entry *find_vme (void *);
void vm_destroy (struct hash *);
void check_valid_buffer (void *, unsigned, void *, bool);
void check_valid_string (const void *, void *);
bool load_file (void *kaddr, struct vm_entry *vme);

#endif
