#include "vm/swap.h"
#include "vm/page.h"

/* 4KB BIMAP SIZE. */
#define BITMAPBITS 12 
#define BITMATSIZE (1 << BITMAPBITS)

struct block *block;
struct bitmap *bitmap;

void 
swap_init (void)
{
	block = block_get_role (BLOCK_SWAP);
	if (block == NULL)
		return;

	bitmap = bitmap_create (BIMAPSIZE);
	if (bitmap == NULL)
		return;

	bitmap_set_all (bitmap, false);
}

void 
swap_in (size_t used_index, void *kaddr)
{
	int i = 0;
	ASSERT (bitmap_test (bitmap, used_index) == false);

	bitmap_flip (bitmap, used_index);

	for (i = 0; i < BITMAPSIZE; i++)
		block_read (block, , kaddr);
}

size_t 
swap_out (void *kaddr)
{
	size_t slot_num;

	slot_num = bitmap_scan_and_flip ();

	for (i = 0; i < BITMAPSIZE; i++)
		block_write (block, , kaddr);

	return slot_num;
}

