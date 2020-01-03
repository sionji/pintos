#ifndef VM_SWAP_H_
#define VM_SWAP_H_

void swap_init (size_t used_index, void *kaddr);
void swap_in (size_t used_index, void *kaddr);
size_t swap_out (void *kaddr);

#endif
