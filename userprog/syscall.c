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

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
void syscall_get_args (void *esp, int *args, int count);
void check_address (void *esp);
void syscall_exit (int exit_status);

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
	int *ptr = f->esp;

	/* Check the esp has valid address. */
  check_address (f->esp);
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
		    status = *(ptr+1);

        //hex_dump (f->esp, f->esp, 100, true);

				syscall_exit (status);
			  break;
			}

		case SYS_EXEC :                   /* Start another process. */
			{
			  struct thread *t_child;
			  char *cmd_line;
			  int tid, retval;
				syscall_get_args (f->esp, args, 1);
			  cmd_line = (char *)*(ptr+1);

			  /* Create thread, load, execute child process. */
			  retval = tid = process_execute (cmd_line);
			  t_child = find_child (tid);
				sema_down (&t_child->sema_load);

			  /* If creating Thread is un-successful, then */ 
        if (t_child == NULL)
				  retval = -1;

			  /* In case that creating thread is successful,
				   but load is not successful. */
			  if (t_child->flag_load != 1)
			  	retval = -1;

			  f->eax = retval;
			  break;
			}

		case SYS_WAIT :                   /* Wait for a child process to die. */
			{
			  int retval, pid;
				syscall_get_args (f->esp, args, 1);
			  pid = *(ptr+1);

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
				
		    name = (char *)*(ptr+16);
	  		initial_size = (int32_t)*(ptr+20);
				
  
		  	f->eax = filesys_create (name, initial_size);
			  break;
			}

		case SYS_REMOVE :                 /* Delete a file. */
			{
			  char *name;
				syscall_get_args (f->esp, args, 1);
			  name = (char *)*(ptr+1);

			  f->eax = filesys_remove (name);
			  break;
			}

		case SYS_OPEN :                   /* Open a file. */
			{
				char *name;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				name = (char *)*(ptr+1);

				file = filesys_open (name);
				f->eax = process_add_file (file);
			  break;
			}

		case SYS_FILESIZE :               /* Obtain a file's size. */
			{
				int fd;
				struct file *file;
				syscall_get_args (f->esp, args, 1);
				fd = (int)*(ptr+1);

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
				int i, fd, retval = 0;
				void *buffer;
				unsigned size;
				struct file *file;
				syscall_get_args (f->esp, args, 3);
				fd = (int)*(ptr+5);
				buffer = (void *)*(ptr+6);
				size = (unsigned)*(ptr+7);

				file = process_get_file (fd);
				lock_acquire (&filesys_lock);
				if (fd == 0)
					retval = 0;/*
					for (i = 0; i < size; i++)
					{
						*((uint8_t *)buffer + i) = input_getc();
						if (*((char *)buffer + i) == '\n')
							break;
						retval++;
					}*/
				else if (fd != 0 && file == NULL)
					f->eax = -1;
				else 
					retval = file_read (file, buffer, size);
				
				lock_release (&filesys_lock);
				f->eax = retval;
				break;
			}

		/* buffer is probably an address of the string to write. */
		case SYS_WRITE :                  /* Write to a file. */
			{
				int fd, retval = 0;
				void *buffer;
				unsigned size;
				struct file *file;

				syscall_get_args (f->esp, args, 3);
				/*
				fd = *(int *)args[0];
				buffer = (void *)*(int *)args[1];
				size = (unsigned)*(int *)args[2];
				*/

				fd = (int)*(ptr+5);
				buffer = (char *)*(ptr+6);
				size = (unsigned)*(ptr+7);
				
				printf ("CALLED variable : %d %x %u \n", fd, buffer, size);
				hex_dump (f->esp, f->esp, 100, true);

				file = process_get_file (fd);
        lock_acquire (&filesys_lock);
				if (fd == 1)
				{
					putbuf(buffer, size);
					retval = size;
				}
				else if (file != NULL)
					retval = file_write (file, buffer, size);
				else if (file == NULL)
					syscall_exit (-1);
       	lock_release (&filesys_lock);
				f->eax = retval;
				break;
			}
			
		case SYS_SEEK :                   /* Change position in a file. */
			{
				int fd;
				unsigned position;
				struct file *file;
				syscall_get_args (f->esp, &args[0], 2);
				fd = (int)*(ptr+16);
				position = (unsigned)*(ptr+20);
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
				fd = (int)*(ptr+1);
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
				fd = (int)*(ptr+1);
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
		check_address (esp + 4 * (i + 1));
   	args[i] = (int *)(esp + 4 * (i + 1));
	}
}

void
check_address (void *esp)
{
	bool success;
	success = (esp > (void *) 0x0804800 || esp < (void *) PHYS_BASE ? true : false);
	if (!success)
		syscall_exit (-1);
	else
  	return;
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

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occured. */
static int
get_user (const uint8_t *uaddr)
{
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:"
			 : "=&a" (result) : "m" (*uaddr));
	return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
			 : "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}

