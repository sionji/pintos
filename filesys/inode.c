#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer_cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    /* Array of disk block number which access directly. */
    block_sector_t direct_map_table [];
    /* Number of indirect-accessing index block. */
    block_sector_t indirect_block_sec;
    /* If it access as double-indirect, first index block number. */
    block_sector_t double_indirect_block_sec;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_read (sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
     
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      
      bc_write (sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* ----------------------------------------------------------- */
/* Added codes for extensible file. */

/* Global variables. */
unsigned int INDIRECT_BLOCK_ENTRIES = 64;
unsigned int DIRECT_BLOCK_ENTREIS = 64;

enum direct_t 
{
  NORMAL_DIRECT = 0;
  INDIRECT;
  DOUBLE_INDIRECT;
  OUT_LIMIT;
};

struct
sector_location 
{
  enum direct_t directness;
  /* Offset of entry to access in the first index block. */
  unsigned int index1;
  /* Offset of entry to access in the second index block. */
  unsigned int index2;
};

struct
inode_indirect_block
{
  block_sector_t map_table [INDIRECT_BLOCK_ENTRIES];
};

/* Transfer data to inode_dist from buffer_cache. */
static bool
get_disk_inode (const struct inode *inode; struct inode_disk *inode_disk)
{
  /* Using bc_read (), read on-disk inode from buffer_cache and
     save it to inode_disk. */
  block_sector_t sector_idx = inode->sector;
  bc_read (sector_idx, inode_disk->direct_map_table [sector_idx],
     0, BLOCK_SECTOR_SIZE, 0);
  /* Return true. */
  return true;
}

/* Verify disk block access method (enum direct_t). */
/* Check 1st index block offset, 2nd index block offset. */
static void
locate_byte (off_t pos, struct sector_location *sec_loc)
{
  off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

  /* If it's method is direct, then */
  if (pos_sector < DIRECT_BLOCK_ENTRIES) 
  {
    /* Update variable of struct sector_location. */
  }

  else if (pos_sector < (off_t) (DIRECT_BLOCK_ENTRIES
        + INDIRECT_BLOCK_ENTRIES))
  {
    /* Update variable of struct sector_location. */
  }

  else if (pos_sector < (off_t) (DIRECT_BLOCK_ENTRIES
        + INDIRECT_BLOC_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)))
  {
    /* Update variable of struct sector_location. */
  }

  else
    sec_loc->directness = OUT_LIMIT;

  return;
}

/* Change offset to byte. */
static inline off_t
map_table_offset (int index)
{
  /* Change offset value to byte and return. */
  return index * sizeof (uint8_t);
}

/* Update newly assigned disk block number to inode_disk. */
static bool
register_sector (struct inode_disk *inode_disk,
    block_sector_t new_sector, struct sector_location sec_loc)
{
  /* Do something. */
  switch (sec_loc.directness)
  {
    case NORMAL_DIRECT :
      {
        /* Update newly assigned disk number to inode_disk. */
        break;
      }

    case INDIRECT :
      {
        void *new_block = malloc (BLOCK_SECTOR_SIZE);
        if (new_block == NULL)
          return false;

        /* Save newly assigned block number to index block. */
        /* Write index block to buffer cache. */
        break;
      }

    case DOUBLE_INDIRECT :
      {
        void *new_block = malloc (BLOCK_SECTOR_SIZE);
        if (new_block == NULL)
          return false;

        /* Save newly assigned block address in 2nd index block, 
           write each index block to buffer cache. */
        break;
      }
      
    default :
      {
        return false;
      }
  }
  //free (new_block);   /* Why free new_block? */
  return true;
}

/* Return disk block number using file offset. */
static block_sector_t
byte to sector (const struct inode_disk *inode_disk, off_t pos)
{
  block_sector_t result_sec;    /* Disk block number to return. */

  if (pos < inode_disk->length)
  {
    struct inode_indirect_block *ind_block;
    struct sector_location sec_loc;
    locate_byte (pos, &sec_loc);  /* Calculate index block offset.*/

    switch (sec_loc.directness)
    {
      case NORMAL_DIRECT : 
        {
          /* Get disk block number from on-disk inode direct_map_table. */
          break;
        }

      case INDIRECT :
        {
          ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
          if (ind_block)
          {
            /* Read index block from buffer cache. */
            /* Check disk block number from index block. */
          }
          else
            result_sec = 0;

          free (ind_block);
          break;
        }
        
      case DOUBLE_INDIRECT :
        {
          ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
          if (ind_block)
          {
            /* Read 1st index block from buffer cache. */
            /* Read 1st index block from buffer cache. */
            /* Check disk block number from 2nd index block.*/
          }
        }

      default :
        result_sec = 0;
    }
    /**/
  }

  /**/
  return result_sec;
}

/* If file offset is larger than it's original file size, 
   then allocate new disk block and update inode. */
/* start_pos : Start offset of file area to increase. 
   end_pos : Last offset of file area to increase. */
bool 
inode_update_file_length (struct inode_disk *inode_disk, 
    off_t start_pos, off_t end_pos)
{
  /* Do something. */

  while (size > 0) 
  {
    /* Calc offset within disk block. */
    if (sector_ofs > 0)
    {
      /* If block_offset is larger than 0, it is already assigned block. */
    }
    else
    {
      /* Assign new disk block. */
      if (free_map_allocate (1, &sector_idx))
      {
        /* Update newly assigned disk block number to inode_disk. */
      }
      else
      {
        free (zeroes);   /* What is zereos? */
        return false;
      }

      /* Initiate new disk block to 0. */
      bc_write (sector_idx, zeroes, 0, BLOCK_SECTOR_SIZE, 0);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
  }
  free (zeroes);
  return true;
}

/* Free every disk block which is allocated to file. */
static void
free_inode_sectors (struct inode_disk *inode_disk)
{
  /**/
  int i, j = 0;

  /* Free disk block assigned as double indirect method. */
  if (inode_dis->double_indirect_block_sec > 0)
  {
    /* Read 1st index block from buffer cache. */
    i = 0;
    /* Secondary index blocks are sequentially accessed 
       through the primary index block.*/
    while (ind_block_1->map_table [i] > 0)
    {
      /**/
      /* Read 2nd index block from buffer cache. */
      j = 0;
      /* Access disk block number saved in 2nd index block. */
      while (ind_block_2->map_table [j] > 0)
      {
        /* Free allocated disk block using free_map update. */
        j++;
      }
      /* Free 2nd index block. */
      i++;
    }
    /* Free 1st index block. */
  }

  /* Free disk block allocated as indirect method. */
  if (inode_disk->indirect_block_sec > 0)
  {
    /**/
    /* Read index block from buffer cache. */
    /* Access disk block number saved in index block. */
    while (ind_block->map_table [i] >0)
    {
      /* Free allocated disk block using free_map update. */
      i++;
    }
    /**/
  }

  /* Free disk block*/
  while (inode_disk->direct_map_table [i] > 0)
  {
    /* Free allocated disk block using free_map update. */
    i++;
  }
}
