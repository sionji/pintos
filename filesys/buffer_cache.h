#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include <stdbool.h>
#include <filesys/inode.h>
#include <filesys/block.h>
#include <filesys/off_t.h>

struct buffer_head 
{
  struct inode *inode;      /* inode pointer. */
  bool dirty;               /* Flag shows dirty. */
  block_sector_t sector;    /* Address of disk sector of it's entry. */
  list_elem clock;          /* Used for clock algorithm. */
  struct lock head_lock;    /* Lock. */
  void *data;               /* Buffer cache entry data pointer. */
}

void bc_init (void);        /* Initiate buffer cache. */
void bc_term (void);        /* Terminate buffer cache. */
bool bc_read (block_sector_t, void *, off_t, int, int);
bool bc_write (block_sector_t, void *, off_t, int, int);
struct buffer_head *bc_lookup (block_sector_t);
struct buffer_head *bc_select_victim (void);
void bc_flush_entry (struct buffer_head *);
void bc_flush_all_entries (void);

#endif 
