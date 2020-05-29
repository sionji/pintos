#include "vm/swap.h"
#include "threads/synch.h"

/* Number of sectors per page. */
/* Sector size is 512(=2^9, which is defined as BLOCK_SECTOR_SIZE),
   so we need as many sectors as (PGSIZE / BLOCK_SECTOR_SIZE) per page. */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* 4KB BITMAP SIZE. */
/* If BITMAP SIZE is too large, then execution time is too long.
   IF BITMAP SIZE is too small, then errors will be occured. 
   It is important that finding a suitable bitmap size. */
/* To make suitable BITMAPSIZE, look at the following suggestions.
   1) PintOS uses 4MB swap size and PGSIZE is 4KB. 
   2) PintOS is operated as block. Each page has SECTORS_PER_PAGE sectors.
   So, (4MB / 4KB * SECTORS_PER_PAGE) = 2^13 BITMAPSIZE is maybe suitable. */
#define BITMAPBITS 13
#define BITMAPSIZE (1 << BITMAPBITS)

struct block *block;
struct bitmap *bitmap;
struct lock swap_lock;

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
  lock_init (&swap_lock);
}

void 
swap_in (size_t used_index, void *kaddr)
{
  int i = 0;

  if (block == NULL || bitmap == NULL)
    return;

  lock_acquire (&swap_lock);
  bitmap_flip (bitmap, used_index);

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (block, used_index * SECTORS_PER_PAGE + i, 
                (char *) kaddr + BLOCK_SECTOR_SIZE * i);
  lock_release (&swap_lock);
}

size_t 
swap_out (void *kaddr)
{
  int i;
  size_t slot_num;

  if (block == NULL || bitmap == NULL)
    return;

  lock_acquire (&swap_lock);
  /* Find empty bitmap slot using First fit. */
  slot_num = bitmap_scan_and_flip (bitmap, 0, 1, false);

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (block, slot_num * SECTORS_PER_PAGE + i, 
                 (char *) kaddr + BLOCK_SECTOR_SIZE * i);
  lock_release (&swap_lock);

  return slot_num;
}

