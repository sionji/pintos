#ifndef VM_SWAP_H_
#define VM_SWAP_H_

#include "threads/vaddr.h"
#include "vm/page.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"

void swap_init (void);
void swap_in (size_t, void *);
size_t swap_out (void *);

#endif
