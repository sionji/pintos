#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct vm_entry {
	uint8_t type;                     /* Type for VM_BIN, VM_FILE, VM_ANON. */
	void *vaddr;
	bool writeable;

	bool is_loaded;
	struct file* file;

	/* For Memory mapped file */
	struct list_elem mmap_elem;

	size_t offset;
	size_t read_bytes;
	size_t zero_bytes;

	/* For Swapping */
	size_t swap_slot;

	/* Data structure for vm_entry */
	struct hash_elem elem;
}
