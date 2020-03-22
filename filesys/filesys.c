#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  /* bc_init () *MUST* be called before do_format () called. */
  /* do_format () format bitmap and block using free_map_create (). 
     free_map_create () calls file_write () and 
     file_write () calls inode_write_at ().
     inode_write_at () calls bc_write (). */
  /* bc_init () *MUST* be called before bc_write () called. 
     That's why bc_init () is located at front of function. */
  bc_init ();

  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

  /* Set root directory. */
  thread_current ()->cur_dir = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  /* Added code for buffer cache. */
  bc_term ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;

  /* Original code. */
  //struct dir *dir = dir_open_root ();
  /* New codes. */
  char path_name [512];
  strlcpy (path_name, name, strlen (name) + 1);
  char file_name [NAME_MAX + 1];
  struct dir *dir = parse_path (path_name, file_name);
  /* PARSE_PATH destroy char string to NULL, maybe needs using copy. */

  bool success = (dir != NULL && !inode_is_removed (dir_get_inode (thread_current ()->cur_dir))
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  /* Original code. */
  //struct dir *dir = dir_open_root ();
  /* New codes. */
  char path_name [512];
  strlcpy (path_name, name, strlen (name) + 1);
  char file_name [NAME_MAX + 1];
  struct dir *dir = parse_path (path_name, file_name);
  if (dir == NULL)
    return NULL;

  struct inode *inode = NULL;
  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);

  dir_close (dir);

  if (inode == NULL || inode_is_removed (inode) || 
      inode_is_removed (dir_get_inode (thread_current ()->cur_dir)))
    return NULL;
  else
    return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  int PATH_LENGTH = strlen (name) + 1;
  char path_name [512];
  strlcpy (path_name, name, PATH_LENGTH);
  char file_name [NAME_MAX + 1];
  struct dir *dir = parse_path (path_name, file_name);

  bool success = false;

  /* Find inode. */
  struct inode *inode;
  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);

  /* Check file is directory. */
  if (inode_is_dir (inode))
  {
    /* In case of directory. */
    struct dir *target_dir = dir_open (inode);

    if (file_name != NULL && !dir_readdir (target_dir, file_name))
      success = dir_remove (dir, file_name);
  }
  else
  {
    /* In case of file. */
    if (file_name != NULL)
      success = dir_remove (dir, file_name);
  }

  dir_close (dir); 
  return success;
}


/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  struct dir *root = dir_open_root ();
  dir_add (root, ".", ROOT_DIR_SECTOR);
  dir_add (root, "..", ROOT_DIR_SECTOR);
  dir_close (root);

  free_map_close ();
  printf ("done.\n");
}

/* Analysis PATH_NAME to return working directory info ptr. */
/* Save file name to FILE_NAME and point opened dir. */
struct dir *
parse_path (char *path_name, char *file_name)
{
  struct dir *dir = NULL;

  if (path_name == NULL || file_name == NULL)
    return NULL;
  if (strlen (path_name) == 0)
    return NULL;

  /* Store directory info according to absolute/related path of PATH_NAME. */
  if (strcmp (path_name, "/") == 0)
  {
    /* File name is needed to open root directory. */
    strlcpy (file_name, ".", 2);
    return dir_open_root ();
  }

  if (path_name [0] == '/')
    /* case that path_name is start with "/", also has subdirectory name.*/
    dir = dir_open_root ();
  else
    dir = dir_reopen (thread_current ()->cur_dir);

  char *token, *nextToken, *savePtr;
  token = strtok_r (path_name, "/", &savePtr);
  nextToken = strtok_r (NULL, "/", &savePtr);

  while (token != NULL && nextToken != NULL)
  {
    /* Search for the file named token in dir and store the info of inode. */
    struct inode *inode = NULL;
    if (!dir_lookup (dir, token, &inode))
    {
      dir_close (dir);
      return NULL;
    }

    /* If inode is file, NULL. */
    if (!inode_is_dir (inode))
    {
      dir_close (dir);
      return NULL;
    }

    /* Free dir info from memory. */
    dir_close (dir);

    /* Store inode directory info to dir. */
    dir = dir_open (inode);

    /* Store path name to search through token. */
    token = nextToken;
    nextToken = strtok_r (NULL, "/", &savePtr);
  }

  /* Save token file name to file_name. */
  strlcpy (file_name, token, strlen (token) + 1);

  /* Return dir info. */
  return dir;
}

bool 
filesys_create_dir (const char *name)
{
  int PATH_LENGTH = strlen (name) + 1;
  char path_name [512];
  strlcpy (path_name, name, PATH_LENGTH);
  char file_name [NAME_MAX + 1];
  struct dir *dir = parse_path (path_name, file_name);

  block_sector_t inode_sector;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 16)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0 && dir != NULL) 
    free_map_release (inode_sector, 1);

  struct inode *inode;
  if (success && dir_lookup (dir, file_name, &inode))
  {
    struct dir *dir_ = dir_open (inode);
    dir_add (dir_, ".", inode_sector);
    dir_add (dir_, "..", inode_get_inumber (dir_get_inode (dir)));
    dir_close (dir_);
  }

  dir_close (dir);

  return success;
}
