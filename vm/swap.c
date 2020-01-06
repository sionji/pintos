#include "vm/swap.h"

/* 4MB BIMAP SIZE. */
#define BITMAPBITS 22 
#define BITMAPSIZE (1 << BITMAPBITS)

/* Number of sectors per page. */
/* Sector size is 512(=2^8, which is defined as BLOCK_SECTOR_SIZE),
   so we need as many sectors as (PGSIZE / BLOCK_SECTOR_SIZE) per page. */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct block *block;
struct bitmap *bitmap;

void 
swap_init (void)
{
	block = block_get_role (BLOCK_SWAP);
	if (block == NULL)
		return;

	bitmap = bitmap_create (BITMAPSIZE);
	if (bitmap == NULL)
		return;

	bitmap_set_all (bitmap, false);
}

void 
swap_in (size_t used_index, void *kaddr)
{
	int i = 0;

	if (block == NULL || bitmap == NULL)
		return;

	bitmap_flip (bitmap, used_index);

	for (i = 0; i < SECTORS_PER_PAGE; i++)
		block_read (block, used_index * SECTORS_PER_PAGE + i, 
				        (uint8_t) kaddr + BLOCK_SECTOR_SIZE * i);
}

size_t 
swap_out (void *kaddr)
{
	int i;
	size_t slot_num;

	if (block == NULL || bitmap == NULL)
		return;

	/* Find empty bitmap slot using First fit. */
	slot_num = bitmap_scan_and_flip (bitmap, 0, 1, false);

	for (i = 0; i < SECTORS_PER_PAGE; i++)
		block_write (block, slot_num * SECTORS_PER_PAGE + i, 
				         (uint8_t) kaddr + BLOCK_SECTOR_SIZE * i);

	return slot_num;
}

