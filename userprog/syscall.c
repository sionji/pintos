#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/input.h"
#include <devices/shutdown.h>
#include <stdio.h>
#include <stdbool.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);
void syscall_get_args (void *esp, int *args, int count);
void do_munmap (struct mmap_file *mmap_file);
int get_mapid (void);
bool syscall_readdir (int fd, char *name);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
	int sysnum = *(int *)(f->esp);
	int args[4];

	/* Check the esp has valid address. */
  check_address (f->esp, f->esp);

	/* System call codes are written in the following order.
	   1. Saves the value in variable via de-referencing. 
	   2. Perform the action appropriate for the system call.
	   3. If syscall needs return value, then save in f->eax.
	   4. Break the switch-case. */
	switch (sysnum) 
	{
		case SYS_HALT :                   /* Halt the operating system. */
			shutdown_power_off();
			break;

		case SYS_EXIT :                   /* Terminate this process. */
			{
			  int status;
				syscall_get_args (f->esp, args, 1);
		    status = args[0];

				syscall_exit (status);
			  break;
			}

		case SYS_EXEC :                   /* Start another process. */
			{
			  struct thread *t_child;
			  char *cmd_line;
			  int tid, retval;
				syscall_get_args (f->esp, args, 1);
			  cmd_line = (char *)args[0];

				/* Check each pointer have valid address. */
				check_valid_string ((void *)cmd_line, f->esp);

			  /* Create thread, load, execute child process. */
			  retval = tid = process_execute (cmd_line);
			  t_child = find_child (tid);
				sema_down (&t_child->sema_load);

			  /* In case that creating thread is successful,
				   but load is not successful. */
			  if (t_child->flag_load == 1)
					f->eax = retval;
				else
				{
					process_wait (tid);
			  	f->eax = -1;
				}

			  break;
			}

		case SYS_WAIT :                   /* Wait for a child process to die. */
			{
			  int retval, pid;
				syscall_get_args (f->esp, args, 1);
			  pid = args[0];

			  /* Wait for pid child process. */
			  retval = process_wait (pid);
			  f->eax = retval;
			  break;
			}

		case SYS_CREATE :                 /* Create a file. */
			{
			  char *name;
			  int32_t initial_size;
				
				syscall_get_args (f->esp, args, 2);
				
		    name = (char *)args[0];
	  		initial_size = (int32_t)args[1];
			
			  /* Check each pointer have valid address. */
				check_valid_string ((void *)name, f->esp);

				if (name == NULL)
					syscall_exit (-1);

		  	f->eax = filesys_create (name, initial_size);
			  break;
			}

		case SYS_REMOVE :                 /* Delete a file. */
			{
			  char *name;
				syscall_get_args (f->esp, args, 1);
			  name = (char *)args[0];

				if (name == NULL)
					syscall_exit (-1);

				/* Check each pointer have valid address. */
				check_valid_string ((void *)name, f->esp);

				lock_acquire (&filesys_lock);
			  f->eax = filesys_remove (name);
				lock_release (&filesys_lock);
			  break;
			}

		case SYS_OPEN :                   /* Open a file. */
			{
				char *name;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				name = (char *)args[0];

				/* Check each pointer have valid address. */
				check_valid_string ((void *)name, f->esp);

				if (name == NULL)
				{
					f->eax = -1;
					break;
				}
				lock_acquire (&filesys_lock);
				file = filesys_open (name);
			  f->eax = process_add_file (file);
				lock_release (&filesys_lock);
			  break;
			}

		case SYS_FILESIZE :               /* Obtain a file's size. */
			{
				int fd;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				fd = (int)args[0];

				file = process_get_file (fd);
				if (file == NULL)
					f->eax = -1;
				else
  				f->eax = file_length (file);
		  	break;
			}

		/* buffer is probably an address of the string to read. */
		case SYS_READ :                   /* Read from a file. */
			{
				int fd = 0;
				void *buffer;
				unsigned size;
				
				syscall_get_args (f->esp, args, 3);
				fd = (int)args[0];
				buffer = (void *)args[1];
				size = (unsigned)args[2];
				
				/* Check each pointer have valid address. */
				check_valid_buffer (buffer, size, f->esp, true);

				f->eax = syscall_read (fd, buffer, size);
				break;
			}

		/* buffer is probably an address of the string to write. */
		case SYS_WRITE :                  /* Write to a file. */
			{
				int fd = 0;
				void *buffer;
				unsigned size;
				
				syscall_get_args (f->esp, args, 3);
				fd = (int)args[0];
				buffer = (void *)args[1];
				size = (unsigned)args[2];

				/* Check each pointer have valid address. */
				check_valid_buffer (buffer, size, f->esp, false);
				//check_valid_string (buffer, f->esp);
				
				f->eax = syscall_write (fd, buffer, size);
				break;
			}
			
		case SYS_SEEK :                   /* Change position in a file. */
			{
				int fd;
				unsigned position;
				struct file *file;
				
				syscall_get_args (f->esp, &args[0], 2);
				fd = (int)args[0];
				position = (unsigned)args[1];

				file = process_get_file (fd);
				if (file != NULL)
					file_seek (file, position); 
				break;
			}

		case SYS_TELL :                   /* Report current position in a file. */
			{
				int fd;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				fd = (int)args[0];
				file = process_get_file (fd);
				if (file != NULL)
					f->eax = file_tell (file);
				break;
			}

		case SYS_CLOSE :                  /* Close a file. */
			{
				int fd;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				fd = (int)args[0];
				file = process_get_file (fd);
				if (file != NULL)
				{
					file_close (file);
					thread_current ()->fdt[fd] = NULL;
				}
				break;
			}

		case SYS_MMAP :
			{
				int fd;
				void *vaddr;
				syscall_get_args (f->esp, args, 2);
				fd = (int)args[0];
				vaddr = (void *)args[1];
				f->eax = syscall_mmap (fd, vaddr);
				break;
			}

		case SYS_MUNMAP :
			{
				mapid_t mapid;
				syscall_get_args (f->esp, args, 1);
				mapid = (mapid_t)args[0];
				syscall_munmap (mapid);
				break;
			}

    case SYS_ISDIR :
      {
        int fd;

        syscall_get_args (f->esp, args, 1);
        fd = (int) args[0];
        struct file *file = process_get_file (fd);

        f->eax = inode_is_dir (file_get_inode (file));
        break;
      }

    case SYS_CHDIR :
      {
        char file_name [NAME_MAX + 1];
        char *name;
        syscall_get_args (f->esp, args, 1);
        name = (char *) args [0];
        int PATH_LENGTH = strlen (name) + 1;
        char path_name [PATH_LENGTH];
        strlcpy (path_name, name, strlen (name) + 1);

        struct inode *inode = NULL;
        struct dir *dir = parse_path (path_name, file_name);
        if (dir != NULL)
        {
          if (dir_lookup (dir, file_name, &inode))
          {
            dir_close (thread_current ()->cur_dir);
            thread_current ()->cur_dir = dir_open (inode);
          }
        }

        f->eax = true;
        break;
      }

    case SYS_MKDIR :
      {
        char *path_name;
        syscall_get_args (f->esp, args, 1);
        path_name = (char *) args [0];

        f->eax = filesys_create_dir (path_name);
        break;
      }

    case SYS_READDIR :
      {
        syscall_get_args (f->esp, args, 2);
        f->eax = syscall_readdir ((int) args [0], (char *) args [1]);
        break;
      }

    case SYS_INUMBER :
      {
        int fd;
        syscall_get_args (f->esp, args, 1);
        fd = (int) args [0];
        struct file *file = process_get_file (fd);
        f->eax = inode_get_inumber (file_get_inode (file));
        break;
      }

    default :
			syscall_exit (-1);
		  break;
	}
}

void
syscall_get_args (void *esp, int *args, int count)
{
	int i;
  for (i = 0; i < count; i++)
	{
		check_address (esp + 4 * (i + 1), esp);
   	args[i] = *(int *)(esp + 4 * (i + 1));
	}
}

struct vm_entry *
check_address (void *addr, void *esp)
{
	if (addr < (void *)0x0804800 || addr >= (void *)0xc0000000)
		syscall_exit (-1);

	struct vm_entry *vme = find_vme (addr);
	if (vme == NULL)
  {
    return;
  }
 	else
		return vme;
}

void
syscall_exit (int exit_status)
{
	struct thread *t = thread_current ();
	t->exit_status = exit_status;
  printf ("%s: exit(%d)\n", thread_name(), exit_status);
	thread_exit ();
  return;
}

int
syscall_read (int fd, void *buffer, unsigned size)
{
	int i, retval = 0;
	struct file *file;
	file = process_get_file (fd);
	lock_acquire (&filesys_lock);
	if (fd == 0)
	{
		retval = 0;
		for (i = 0; i < size; i++)
		{
			*((uint8_t *)buffer + i) = input_getc();
			if (*((char *)buffer + i) == '\n')
				break;
			retval++;
		}
	}
	else if (fd == 1 || file == NULL)
		retval = -1;
	else
		retval = file_read (file, buffer, size);
		
	lock_release (&filesys_lock);

	return retval;
}

int
syscall_write (int fd, void *buffer, unsigned size)
{
	int retval;
	struct file *file;
	file = process_get_file (fd);
  lock_acquire (&filesys_lock);

	if (fd == 1)
	{
		putbuf(buffer, size);
		retval = size;
	}
	else if (file == NULL || fd == 0)
		retval = 0;
  /* Make sure that file is not directory. */
  else if (!inode_is_dir (file_get_inode (file)))
		retval = file_write (file, buffer, size);
  else
    retval = -1;
 	lock_release (&filesys_lock);
  return retval;
}

int 
get_mapid (void)
{
	int mapid = thread_current ()->next_mapid++;
	return mapid;
}

int 
syscall_mmap (int fd, void *addr)
{
	if ((uint32_t) addr % PGSIZE != 0 || addr == NULL)
		return -1;

	struct file *file = process_get_file (fd);
	file = file_reopen (file);
	if (file == NULL)
		return -1;

	struct mmap_file *mmap_file = (struct mmap_file *)malloc (sizeof (struct mmap_file));
	if (mmap_file == NULL)
		return -1;

	/* Initialize. */
	mapid_t mapid = get_mapid ();  /* Supplement. */
	mmap_file->mapid = mapid;
	mmap_file->file = file;
	list_init (&mmap_file->vme_list);

	/* Demand paging. */
	int32_t ofs = 0;
	uint32_t read_bytes = file_length (file);
  while (read_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

			/* Erase physical memory allocation and mapping codes.
				 Add vm_entry codes. */
			struct vm_entry *vme = (struct vm_entry *) malloc (sizeof (struct vm_entry));
			if (vme == NULL)
				return -1;
			vme->type = VM_FILE;
			vme->vaddr = addr;
			vme->writable = true;
			vme->is_loaded = false;
			vme->file = file;
			vme->offset = ofs;            /* Which value is needed? */
			vme->read_bytes = page_read_bytes;
			vme->zero_bytes = page_zero_bytes;

			/* Insert vm_entry to hash table page entry. */
			if (!insert_vme (&thread_current ()->vm, vme))
			{
				do_munmap (mmap_file);      /* Must add unmap codes later. */
				free (mmap_file);
				return -1;
			}

			/* Add to vme_list. */
			list_push_back (&mmap_file->vme_list, &vme->mmap_elem);

      /* Advance. */
      read_bytes -= page_read_bytes;
      addr += PGSIZE;           
			ofs += page_read_bytes;
    }

	/* Add element to mmap_list. */
	list_push_back (&thread_current ()->mmap_list, &mmap_file->elem);

	return mapid;
}

/* Remove every vm_entry related to vme_list. */
void
do_munmap (struct mmap_file *mmap_file)
{
	struct list_elem *e = list_begin (&mmap_file->vme_list);
	while (!list_empty (&mmap_file->vme_list))
	{
		struct vm_entry *vme = list_entry (e, struct vm_entry, mmap_elem);
		if (vme->is_loaded)
		{
			if (pagedir_is_dirty (thread_current ()->pagedir, vme->vaddr))
			{
				lock_acquire (&filesys_lock);
				file_write_at (vme->file, vme->vaddr, vme->read_bytes, vme->offset);
				lock_release (&filesys_lock);
			}
			/* If vme is loaded (physical page is exist), free page. */
			//palloc_free_page (pagedir_get_page (thread_current ()->pagedir, vme->vaddr));
			free_page (pagedir_get_page (thread_current ()->pagedir, vme->vaddr));
			pagedir_clear_page (thread_current ()->pagedir, vme->vaddr);
		}

		/* Remove vme from vme_list. */
		e = list_remove (&vme->mmap_elem);
		/* Remove vme from hash table page entry. */
		delete_vme (&thread_current ()->vm, vme);
	}
	/* Don't free vme because file_close () will free it's file. */
}

void 
syscall_munmap (mapid_t mapid)
{
	struct thread *cur = thread_current ();
	struct list_elem *e = list_begin (&cur->mmap_list);
	while (e != list_end (&cur->mmap_list))
	{
		struct mmap_file *mmap_file = list_entry (e, struct mmap_file, elem);
		if (mapid == mmap_file->mapid || mapid == CLOSE_ALL)
		{
			/* Remove vm_entry. */
			/* Remove page table entry. */
			do_munmap (mmap_file);

			/* File close. */
			file_close (mmap_file->file);

			/* Remove mmap_file from list. */
			e = list_remove (e);

			/* Remove mmap_file. */
			free (mmap_file);
		}
		else
			e = list_next (e);
	}
}

bool
syscall_readdir (int fd, char *name)
{
  struct file *file = process_get_file (fd);
  struct inode *inode = file_get_inode (file);
  if (!inode_is_dir (inode))
    return false;

  if (!dir_readdir ((struct dir *)file, name))
    return false;

  return true;
}

