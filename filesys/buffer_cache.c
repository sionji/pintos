#include "filesys/buffer_cache.h"

/* Number of cache entry (32KByte). */
#define BUFFER_CACHE_ENTRY_NB (1 << 15) 

void *p_buffer_cache;            /* Indicates buffer cache memory space. */
int *buffer_head [64];           /* Array of buffer head. */
struct list_elem *clock_hand;    /* Victim entry chooser at clock algorithm. */

void
bc_init (void)
{
  /* Allocate buffer cache in memory. */
  /* p_buffer cache points buffer cache. */
  /* Initiate global variable buffer_head. */
}

bool
bc_read (block_sector_t sector_idx, void *bufer,
         off_t bytes_read, int chunk_size, int sector_ofs)
{
  /* Do something. */
}

bool
bc_write (block_sector_t sector_idx, void *buffer,
          off_t bytes_written, int chunk_size, int sector_ofs)
{
  bool success = false;

  /* Do something. */

  return success'
}

/* Flush cached data to Disk block. */
/* Free buffer cache space from memory. */
void
bc_term (void)
{
  /* Using bc_flush_all_entries to flush every buffer cache to disk. */
  /* Deallocate buffer cache memory space. */
}

/* Choose victim entry using clock algorithm. 
   If victim entry is dirty, flush data to disk. */
struct buffer_head *
bc_select_victim (void)
{
  /* Choose victim using clock algorithm. */
  /* Traverse buffer_head and check clock_bit. */
  /* If selected victim entry is dirty, flush.*/
  /* Update buffer_head of victim entry. */
  /* Return victim entry. */
}

/* Traverse buffer_head and check disk block is cached or not. 
   If cached, return buffer cache entry. 
   If not, return NULL. */
struct buffer_head *
bc_lookup (block_sector_t sector)
{
  /* Traverse buffer_head, find buffer cache entry which has 
     same block_sector_t number. */
}

/* Flush buffer cache data to disk : block_write (). */
void 
bc_flush_entry (struct buffer_head *p_flush_entry)
{
  /* Call block_write, flush buffer cache entry data to disk. */
  /* Update buffer_head's dirty bit. */
}

/* Traverse buffer_head and flush dirty entry data to disk. */
void
bc_flush_all_entries (void)
{
  /* Traverse buffer_head, flush entry to disk if it is dirty,
     using block_write (). */
  /* After flush, update dirty bit of buffer cache. */
}
