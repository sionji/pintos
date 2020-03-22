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

/* Added code for extensible file. */
#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES 128 

enum direct_t 
{
  NORMAL_DIRECT = 0,
  INDIRECT,
  DOUBLE_INDIRECT,
  OUT_LIMIT
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

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_dir;                    /* 0 : file, 1 : directory. */
    /* Array of disk block number which access directly. */
    block_sector_t direct_map_table [DIRECT_BLOCK_ENTRIES];
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
    struct lock extend_lock;            /* Lock when aceessing inode. */
  };

static bool get_disk_inode (const struct inode *, struct inode_disk *);
static void locate_byte (off_t pos, struct sector_location *);
static inline off_t map_table_offset (int index);
static bool register_sector (struct inode_disk *, block_sector_t,
    struct sector_location);
bool inode_update_file_length (struct inode_disk *, off_t, off_t);
block_sector_t alloc_indirect_index_block (void);
static void free_inode_sectors (struct inode_disk *);

/* Modified codes for extensible file. */
/* Return disk block number using file offset. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos)
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
          result_sec = inode_disk->direct_map_table [sec_loc.index1];
          break;
        }

      case INDIRECT :
        {
          ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
          if (ind_block)
          {
            /* Read index block from buffer cache. */
            bc_read (inode_disk->indirect_block_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0);
            /* Check disk block number from index block. */
            result_sec = ind_block->map_table [sec_loc.index1];
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
            bc_read (inode_disk->double_indirect_block_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0); 
            block_sector_t next_idx = ind_block->map_table [sec_loc.index1];
            /* Read 2nd index block from buffer cache. */
            bc_read (next_idx, ind_block, 0, BLOCK_SECTOR_SIZE, 0);
            /* Check disk block number from 2nd index block. */
            result_sec = ind_block->map_table [sec_loc.index2];
          }
          else
            result_sec = 0;

          free (ind_block);
          break;
        }

      default :
        result_sec = 0;
    }
    /**/
  }
  /**/
  return result_sec;
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
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
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
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->indirect_block_sec = 0;
      disk_inode->double_indirect_block_sec = 0;
      disk_inode->is_dir = is_dir;

      /* Added codes. */
      if (length > 0)
        inode_update_file_length (disk_inode, 0, length);
		
      bc_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
      free (disk_inode);
      success = true;
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

  /* Fixed code for extensible file. */
  lock_init (&inode->extend_lock);
  //block_read (fs_device, inode->sector, &inode->data);
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
          /* Get on-disk inode structrue by get_disk_inode (). */
          struct inode_disk *disk_inode = (struct inode_disk *) malloc (BLOCK_SECTOR_SIZE);
          get_disk_inode (inode, disk_inode);
          /* Deallocate each blocks by free_inode_sectors (). */
          free_inode_sectors (disk_inode);
          /* Deallocate on-disk inode by free_map_release (). */
          free_map_release (inode->sector, 1);
          /* Deallocate disk_inode. */
          free (disk_inode);
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

  /* Added codes for extensible file. */
  struct inode_disk *inode_disk = malloc (BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return 0;
  get_disk_inode (inode, inode_disk);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
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

  free (inode_disk);
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

  if (inode->deny_write_cnt)
    return 0;

  /* Added codes for extensible file. */
  struct inode_disk *inode_disk = malloc (BLOCK_SECTOR_SIZE);
  if (inode_disk == NULL)
    return 0;
  get_disk_inode (inode, inode_disk);

  lock_acquire (&inode->extend_lock);
  int old_length = inode_disk->length;
  int write_end = offset + size - 1;

  if (write_end > old_length - 1)
  {
    /* Update length info before call inode_update_file_length (). */
		inode_disk->length = write_end + 1;
    /* Update on-disk inode. */
		inode_update_file_length (inode_disk, old_length, write_end);

		bc_write (inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
  }
  lock_release (&inode->extend_lock);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      
      bc_write (sector_idx, (void *)buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  /* Write modified struct disk_inode to buffer cache. */
  bc_write (inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  free (inode_disk);
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
  struct inode_disk inode_disk;
  get_disk_inode (inode, &inode_disk);
  return inode_disk.length;
}

/* ----------------------------------------------------------- */
/* Added codes for extensible file. */

/* Transfer data to inode_disk from buffer_cache. */
static bool
get_disk_inode (const struct inode *inode, struct inode_disk *inode_disk)
{
  /* Using bc_read (), read on-disk inode from buffer_cache and
     save it to inode_disk. */
  if (bc_read (inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0))
    return true;
  else
    return false;
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
    sec_loc->directness = NORMAL_DIRECT;
    sec_loc->index1 = pos_sector;
  }

  else if (pos_sector < (off_t) (DIRECT_BLOCK_ENTRIES
        + INDIRECT_BLOCK_ENTRIES))
  {
    /* Update variable of struct sector_location. */
    sec_loc->directness = INDIRECT;
    sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
  }

  else if (pos_sector < (off_t) (DIRECT_BLOCK_ENTRIES
        + INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)))
  {
    /* Update variable of struct sector_location. */
    sec_loc->directness = DOUBLE_INDIRECT;
    pos_sector = pos_sector - (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES);
    sec_loc->index1 = pos_sector / INDIRECT_BLOCK_ENTRIES;
    sec_loc->index2 = pos_sector % INDIRECT_BLOCK_ENTRIES;
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
  return index * sizeof (uint32_t);
}

/* Update newly assigned disk block number to inode_disk. */
static bool
register_sector (struct inode_disk *inode_disk,
    block_sector_t new_sector, struct sector_location sec_loc)
{
  /* Do something. */
  int i = 0;
  switch (sec_loc.directness)
  {
    case NORMAL_DIRECT :
      {
        /* Update newly assigned disk number to inode_disk. */
        inode_disk->direct_map_table [sec_loc.index1] = new_sector;
        break;
      }

    case INDIRECT :
      {
        struct inode_indirect_block *new_block = malloc (BLOCK_SECTOR_SIZE);
        if (new_block == NULL)
          return false;

        /* In case that indirect block is already exist. */
        if (inode_disk->indirect_block_sec > 0)
          bc_read (inode_disk->indirect_block_sec, new_block, 0, BLOCK_SECTOR_SIZE, 0);
        /* In case that indirect block is not exist yet.*/
        else
        { 
          /* Allocater indirect index block. */
          if (!free_map_allocate (1, &inode_disk->indirect_block_sec))
          {
            free (new_block);
            return false;
          }
          else
          {
            /* Initialize. */
            for (i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
              new_block->map_table [i] = 0;
          }
        }

        /* Save new sector number to map table. */
        new_block->map_table [sec_loc.index1] = new_sector;
        /* Write indirect index block to buffer cache. */
        /* Write indirect block, not buffer block, so
           bytes_written  = map_table_offset (),
           chunk_size = sizeof (uint32_t) = 4 bytes,
           sector_ofs = map_table_offset (). */
        bc_write (inode_disk->indirect_block_sec, new_block, 0, BLOCK_SECTOR_SIZE, 0);
        free (new_block);
        break;
      }

    case DOUBLE_INDIRECT :
      {
        struct inode_indirect_block *first_block = malloc (BLOCK_SECTOR_SIZE);
        if (first_block == NULL)
          return false;

        /* Read first indirect index block. */
        /* In case that double indirect block is already exist. */
        if (inode_disk->double_indirect_block_sec > 0)
          bc_read (inode_disk->double_indirect_block_sec, first_block, 0, BLOCK_SECTOR_SIZE, 0);
        /* In case that double indirect block is not exist yet. */
        else
        {
          if (!free_map_allocate (1, &inode_disk->double_indirect_block_sec))
          {
            free (first_block);
            return false;
          }
          else
          {
            /* Initialize. */
            for (i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
              first_block->map_table [i] = 0;
          }
        }
        
        /* Read second indirect index block. */
        struct inode_indirect_block *second_block = malloc (BLOCK_SECTOR_SIZE);
        if (second_block == NULL)
          return false;

        /* In case that second index block is exist. */
        if (first_block->map_table [sec_loc.index1] > 0)
          bc_read (first_block->map_table [sec_loc.index1], second_block, 0, BLOCK_SECTOR_SIZE, 0);
        /* In case that second index block is not exist yet.*/
        else
        {
          if (!free_map_allocate (1, &first_block->map_table [sec_loc.index1]))
          {
            free (first_block);
            free (second_block);
            return false;
          }
          else
          {
            for (i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
              second_block->map_table [i] = 0;
            /* First indirect block may be modified potentially. */
            bc_write (inode_disk->double_indirect_block_sec, first_block, 0, BLOCK_SECTOR_SIZE, 0);
          }
        }

        /* Save new sector number to map table. */
        second_block->map_table [sec_loc.index2] = new_sector;
        /* Write indirect index block to buffer_cache. */
        bc_write (first_block->map_table [sec_loc.index1], second_block, 0, BLOCK_SECTOR_SIZE, 0);
        free (first_block);
        free (second_block);
        break;
      }
      
    default :
      {
        return false;
      }
  }
  return true;
}

/* If file offset is larger than it's original file size, 
   then allocate new disk block and update inode. */
/* start_pos : Start offset of file area to increase. 
   end_pos : Last offset of file area to increase. */
/* WARNING : It does not update inode_disk length info. */
bool 
inode_update_file_length (struct inode_disk *inode_disk, 
    off_t start_pos, off_t end_pos)
{
  /* Do something. */
  off_t size = end_pos - start_pos;  /* Size to increase. */
	off_t offset = start_pos;          /* Offset of whole file length. */

  block_sector_t sector_idx;         /* Block sector index. */
  struct sector_location sec_loc;    /* Sector location indicator. */

	/* Set zeroes. */
  /* We know how much file length to increase, not data info. 
     So we assign zero-initiated block to increased block. */
  char *zeroes = malloc (BLOCK_SECTOR_SIZE);
  if (zeroes == NULL)
    return false;
  memset (zeroes, 0, BLOCK_SECTOR_SIZE);

  while (size > 0) 
  {
    /* Calc offset within disk block. */
    /* SECTOR_OFS is offset of one block, not whole file length offset. */
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int inode_left = inode_disk->length - offset;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs > 0)
    {
      /* If block_offset is larger than 0, it is already assigned block. */
      /* Do what??? */
    }
    else
    {
      /* Assign new disk block. */
      if (free_map_allocate (1, &sector_idx))
      {
        /* Update newly assigned disk block number to inode_disk. */
        locate_byte (offset, &sec_loc);
        register_sector (inode_disk, sector_idx, sec_loc);
      }
      else
      {
        free (zeroes);
        return false;
      }

      /* Initiate new disk block which is initialized as 0. */
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
  struct inode_indirect_block *ind_block, *ind_block_1, *ind_block_2;

  /* Free disk block assigned as double indirect method. */
  if (inode_disk->double_indirect_block_sec > 0)
  {
    /* Read 1st index block from buffer cache. */
    ind_block_1 = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
    bc_read (inode_disk->double_indirect_block_sec, 
             ind_block_1, 0, BLOCK_SECTOR_SIZE, 0);
    i = 0;
    /* Secondary index blocks are sequentially accessed 
       through the primary index block.*/
    while (ind_block_1->map_table [i] > 0)
    {
      /**/
      /* Read 2nd index block from buffer cache. */
      ind_block_2 = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      bc_read (ind_block_1->map_table [i], ind_block_2, 0, BLOCK_SECTOR_SIZE, 0);
      j = 0;
      /* Access disk block number saved in 2nd index block. */
      while (ind_block_2->map_table [j] > 0)
      {
        /* Free allocated disk block using free_map update. */
        free_map_release (ind_block_2->map_table [j], 1);
        j++;
      }
      /* Free 2nd index block. */
      free (ind_block_2);
      free_map_release (ind_block_1->map_table [i], 1);
      i++;
    }
    /* Free 1st index block. */
    free (ind_block_1);
    free_map_release (inode_disk->double_indirect_block_sec, 1);
  }

  /* Free disk block allocated as indirect method. */
  if (inode_disk->indirect_block_sec > 0)
  {
    /**/
    ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
    /* Read index block from buffer cache. */
    bc_read (inode_disk->indirect_block_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0);
    i = 0;
    /* Access disk block number saved in index block. */
    while (ind_block->map_table [i] >0)
    {
      /* Free allocated disk block using free_map update. */
      free_map_release (ind_block->map_table [i], 1);
      i++;
    }
    /**/
    free (ind_block);
    free_map_release (inode_disk->indirect_block_sec, 1);
  }

  /* Free disk block*/
  i = 0;
  while (inode_disk->direct_map_table [i] > 0)
  {
    /* Free allocated disk block using free_map update. */
    free_map_release (inode_disk->direct_map_table [i], 1);
    i++;
  }
}

bool
inode_is_dir (const struct inode *inode)
{
  bool result = false;

  if (inode == NULL)
    return false;

  /* Allocate inode_disk to memory. */
  struct inode_disk *disk_inode = (struct inode_disk *)malloc (sizeof (struct inode_disk));
  if (disk_inode == NULL)
    return false;

  /* Store inode_disk info from on-disk inode. */
  get_disk_inode (inode, disk_inode);

  /* Return on-disk inode info. */
  result = (disk_inode->is_dir == 1 ? true : false);

  free (disk_inode);
  return result;
}

bool
inode_is_opened (struct inode *inode)
{
  struct list_elem *e;
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes); e = list_next (e))
  {
    struct inode *cur_inode = list_entry (e, struct inode, elem);
    if (inode == cur_inode)
      return true;
  }

  return false;
}

bool
inode_is_removed (struct inode *inode)
{
  return inode->removed;
}
