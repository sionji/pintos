#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/shutdown.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
bool syscall_get_args (void *esp, int *arg, int count);
bool check_address (void *esp);
int syscall_get_cnt (int sysnum);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
	int sysnum = *(int *)(f->esp);
	struct thread *t = thread_current ();
	int arg[3];
	bool success;

	/* Check the esp has valid address. */
	if ((void *)f->esp < (void *)0x0804800 || (void *)f->esp > (void *)PHYS_BASE)
		goto EXIT;

	/* Check address of each esp and save its address to arg[]. */
	success = syscall_get_args (f->esp, arg, syscall_get_cnt (sysnum));
	if (!success)
		goto EXIT;

	/* System call codes are written in the following order.
	   1. Saves the value in variable via de-referencing. 
	   2. Perform the action appropriate for the system call.
	   3. If syscall needs return value, then save in f->eax.
	   4. Break the switch-case. */
  printf ("system call!\n");
	switch (sysnum) 
	{
		case SYS_HALT :                   /* Halt the operating system. */
			shutdown_power_off();
			break;

		case SYS_EXIT :                   /* Terminate this process. */
			{
			  int status;
		    f->eax = status = *(int *)arg[0];

			  t->exit_status = status;
			  printf ("Process %s will exit with status '%d'. \n", t->name, status);
			  thread_exit ();
			  break;
			}

		case SYS_EXEC :                   /* Start another process. */
			{
			  struct thread *t_child;
			  char *cmd_line;
			  int tid, retval;
			  cmd_line = (char *)arg[0];

			  /* Create thread, load, execute child process. */
			  retval = tid = process_execute (cmd_line);
			  t_child = find_child (tid);

			  /* If creating Thread is successful, 
				   then sema_down for waiting.*/
        if (t_child == NULL)
				  retval = -1;
			  else
				  sema_down (&t_child->sema_load);

			  /* In case that creating thread is successful,
				   but load is not successful. */
			  if (t_child != NULL && t_child->flag_load != 1)
			  	retval = -1;

			  f->eax = *(int *)retval;
			  break;
			}

		case SYS_WAIT :                   /* Wait for a child process to die. */
			{
			  int retval, pid;
			  pid = *(int *)arg[0];

			  /* Wait for pid child process. */
			  retval = process_wait (pid);
			  f->eax = retval;
			  break;
			}

		case SYS_CREATE :                 /* Create a file. */
			{
			  char *name;
			  int32_t initial_size;
		    name = (char *)arg[0];
	  		initial_size = *(int32_t *)arg[1];
  
		  	f->eax = filesys_create (name, initial_size);
			  break;
			}

		case SYS_REMOVE :                 /* Delete a file. */
			{
			  char *name;
			  name = (char *)arg[0];

			  f->eax = filesys_remove (name);
			  break;
			}

		case SYS_OPEN :                   /* Open a file. */
			{
				char *name;
				struct file *fd;
				name = (char *)arg[0];
				fd = filesys_open (name);
			  break;
			}

		case SYS_FILESIZE :               /* Obtain a file's size. */
			{
				int fd;
				struct file *file;
				fd = *(int *)arg[0];
		  	break;
			}

		case SYS_READ :                   /* Read from a file. */
			break;

		case SYS_WRITE :                  /* Write to a file. */
			break;

		case SYS_SEEK :                   /* Change position in a file. */
			break;

		case SYS_TELL :                   /* Report current position in a file. */
			break;

		case SYS_CLOSE :                  /* Close a file. */
			break;

    default :
		  break;
	}

  EXIT :
    thread_exit ();
}

bool
syscall_get_args (void *esp, int *arg, int count)
{
	int i;
	bool success;
	if (count == 0)
	  success = check_address (esp);

  for (i = 0; i < count; i++)
	{
		success = check_address (esp + 4 * (i + 1));
		
		if (success)
  		arg[i] = *(int *)(esp + 4 * (i + 1));
		else
			break;
	}
	return success;
}

bool
check_address (void *esp)
{
	return (esp > (void *) 0x0804800 || esp < (void *)PHYS_BASE ? true : false);
}

struct thread *
find_child (int tid)
{
	struct thread *t = thread_current ();
	struct list_elem *e;
	for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
					 e = list_next (e))
	{
		struct thread *t_child = list_entry (e, struct thread, child_elem);
		if (tid == t_child->tid)
			return t_child;
	}

	return NULL;
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

int
syscall_get_cnt (int sysnum)
{
	switch (sysnum)
	{
    /* Projects 2 and later. */
    SYS_HALT :                   /* Halt the operating system. */
		  return 0;
    SYS_EXIT :                   /* Terminate this process. */
    SYS_EXEC :                   /* Start another process. */
    SYS_WAIT :                  /* Wait for a child process to die. */
			return 1;
    SYS_CREATE :                 /* Create a file. */
			return 2;
    SYS_REMOVE :                 /* Delete a file. */
    SYS_OPEN :                   /* Open a file. */
    SYS_FILESIZE :               /* Obtain a file's size. */
			return 1;
    SYS_READ :                   /* Read from a file. */
    SYS_WRITE :                  /* Write to a file. */
			return 3;
    SYS_SEEK :                   /* Change position in a file. */
			return 2;
    SYS_TELL :                   /* Report current position in a file. */
    SYS_CLOSE :                  /* Close a file. */
			return 1;

    /* Project 3 and optionally project 4. */
    SYS_MMAP :                    /* Map a file into memory. */
			return 2;
    SYS_MUNMAP :                 /* Remove a memory mapping. */
			return 1;

    /* Project 4 only. */
    SYS_CHDIR :                  /* Change the current directory. */
    SYS_MKDIR :                  /* Create a directory. */
			return 1;
    SYS_READDIR :                /* Reads a directory entry. */
			return 2;
    SYS_ISDIR :                  /* Tests if a fd represents a directory. */
    SYS_INUMBER :                 /* Returns the inode number for a fd. */
			return 1;
	}
}
