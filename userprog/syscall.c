#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include <devices/shutdown.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);
void syscall_get_args (void *esp, int *args, int count);
struct vm_entry *check_address (void *addr, void *esp UNUSED);

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
				struct file *file;
				
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
check_address (void *addr, void *esp UNUSED)
{
	if (addr < (void *)0x0804800 || addr >= (void *)0xc0000000)
		syscall_exit (-1);

	struct vm_entry *vme = find_vme (addr);
	if (vme == NULL)
		return;
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
  else
		retval = file_write (file, buffer, size);
 	lock_release (&filesys_lock);
  return retval;
}
