#include "filesys/buffer_cache.h"
#include "threads/malloc.h"
#include <string.h>

/* Number of cache entry (32KByte). */
#define BUFFER_CACHE_ENTRY_NB 64 

/* Indicates buffer cache memory space. */
uint8_t *p_buffer_cache;
/* Array of buffer head. */
struct buffer_head buffer_head [BUFFER_CACHE_ENTRY_NB];
/* Victim entry chooser at clock algorithm. */
int clock_hand;

struct lock general;

void
bc_init (void)
{
  /* Allocate buffer cache in memory. */
  p_buffer_cache = malloc (sizeof (BLOCK_SECTOR_SIZE) * BUFFER_CACHE_ENTRY_NB);
  /* p_buffer_cache points buffer cache. */
  /* Initiate global variable buffer_head. */
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_ENTRY_NB; i++)
  {
    buffer_head [i].dirty = false;
    buffer_head [i].clock_bit = false;
    buffer_head [i].data = p_buffer_cache [i];
    buffer_head [i].sector = 0;
    buffer_head [i].valid = false;
    lock_init (&buffer_head [i].head_lock);
  }
  clock_hand = 0;

  fs_device = block_get_role (BLOCK_FILESYS);
  lock_init (&general);
}

/* Flush cached data to Disk block. */
/* Free buffer cache space from memory. */
void
bc_term (void)
{
  /* Using bc_flush_all_entries to flush every buffer cache to disk. */
  bc_flush_all_entries ();
  /* Deallocate buffer cache memory space. */
  free (p_buffer_cache);
}

bool
bc_read (block_sector_t sector_idx, void *buffer,
         off_t bytes_read, int chunk_size, int sector_ofs)
{
  lock_acquire (&general);
  /* Search sector_idx in buffer_head. */
  struct buffer_head *head_ptr = bc_lookup (sector_idx);
  /* If it isn't exist, find victim. */
  if (head_ptr == NULL)
  {
    head_ptr = bc_select_victim ();
    lock_acquire (&head_ptr->head_lock);
    block_read (fs_device, sector_idx, head_ptr->data);
  }
  else
    lock_acquire (&head_ptr->head_lock);
  lock_release (&general);

  /* Using memcpy to copy disk block data to buffer. */
  memcpy (buffer + bytes_read, head_ptr->data + sector_ofs, chunk_size);

  /* clock_bit setting. */
  head_ptr->clock_bit = true;
  lock_release (&head_ptr->head_lock);

  return true;
}

bool
bc_write (block_sector_t sector_idx, void *buffer,
          off_t bytes_written, int chunk_size, int sector_ofs)
{
  bool success = false;

  lock_acquire (&general);
  /* Search sector_ids in buffer_head and copy to buffer cache. */
  struct buffer_head *head_ptr = bc_lookup (sector_idx);
  if (head_ptr == NULL)
  {
    head_ptr = bc_select_victim ();
    lock_acquire (&head_ptr->head_lock);
    block_read (fs_device, sector_idx, head_ptr->data);
  }
  else
    lock_acquire (&head_ptr->head_lock);
  lock_release (&general);

  memcpy (head_ptr->data + sector_ofs, buffer + bytes_written, chunk_size);

  /* Update buffer head. */
  head_ptr->clock_bit = true;
  head_ptr->dirty = true;
  head_ptr->valid = true;
  head_ptr->sector = sector_idx;
  lock_release (&head_ptr->head_lock);

  return success;
}

/* Choose victim entry using clock algorithm. 
   If victim entry is dirty, flush data to disk. */
struct buffer_head *
bc_select_victim (void)
{
  /* Choose victim using clock algorithm. */
  /* Traverse buffer_head and check clock_bit. */
  while (buffer_head [clock_hand].clock_bit == true)
  {
    buffer_head [clock_hand].clock_bit = false;
    clock_hand++;
    if (clock_hand == BUFFER_CACHE_ENTRY_NB)
      clock_hand = 0;
  }

  lock_acquire (&buffer_head [clock_hand].head_lock);
  /* If selected victim entry is dirty, flush.*/
  if (buffer_head [clock_hand].dirty == true &&
      buffer_head [clock_hand].valid == true)
    bc_flush_entry (&buffer_head [clock_hand]);
  /* Update buffer_head of victim entry. */
  buffer_head [clock_hand].dirty = false;
  buffer_head [clock_hand].clock_bit = false;
  buffer_head [clock_hand].sector = 0;
  buffer_head [clock_hand].valid = false;
  lock_release (&buffer_head [clock_hand].head_lock);

  /* Return victim entry. */
  return &buffer_head [clock_hand];
}

/* Traverse buffer_head and check disk block is cached or not. 
   If cached, return buffer cache entry. 
   If not, return NULL. */
struct buffer_head *
bc_lookup (block_sector_t sector)
{
  /* Traverse buffer_head, find buffer cache entry which has 
     same block_sector_t number. */
  int i = 0;
  bool success = false;
  for (i = 0; i < BUFFER_CACHE_ENTRY_NB; i++)
  {
    if (buffer_head [i].valid == true && 
        buffer_head [i].sector == sector)
    {
      success = true;
      break;
    }
  }
  if (success)
    return &buffer_head [i];
  else
    return NULL;
}

/* Flush buffer cache data to disk : block_write (). */
void 
bc_flush_entry (struct buffer_head *buffer_head)
{
  /* Call block_write, flush buffer cache entry data to disk. */
  block_write (fs_device, buffer_head->sector, buffer_head->data);
  /* Update buffer_head's dirty bit. */
  buffer_head->dirty = false;
}

/* Traverse buffer_head and flush dirty entry data to disk. */
void
bc_flush_all_entries (void)
{
  /* Traverse buffer_head, flush entry to disk if it is dirty,
     using block_write (). */
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_ENTRY_NB; i ++)
  {
    if (buffer_head [i].valid == true &&
        buffer_head [i].dirty == true)
      bc_flush_entry (&buffer_head [i]);      /* Flush. */
    /* After flush, update dirty bit of buffer cache. */
    buffer_head [i].dirty = false;
  }
}
