#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
void syscall_get_args (void *esp, int *arg, int count);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
	uint8_t sysnum = *(f->esp);
	int arg[3];

	if ((void *)f->esp < 0x0804800 || (void *)f->esp > PHYS_BASE)
		goto EXIT;

  printf ("system call!\n");
	switch (sysnum) 
	{
		case SYS_HALT :                   /* Halt the operating system. */
			shutdown_power_off();
			break;

		case SYS_EXIT :                   /* Terminate this process. */
			int status;
			struct thread *t = thread_current ();
			syscall_get_args (f->esp, arg, 1);
			f->eax = status = *(int *)arg[0];
			printf ("Process %s will exit with status '%d'. \n", t->name, status);
			thread_exit ();
			break;

		case SYS_EXEC :                   /* Start another process. */
			const char *cmd_line;
			tid_t tid;
			syscall_get_args (f->esp, arg, 1);
			cmd_line = (char *)arg[0];
			tid = process_execute (cmd_line);
			break;

		case SYS_WAIT :                   /* Wait for a child process to die. */
			pid_t pid;
			syscall_get_args (f->esp, arg, 1);
			pid = *(pid_t *)arg[0];
			wait();
			break;

		case SYS_CREATE :                 /* Create a file. */
			const char *name;
			off_t initial_size;
			syscall_get_args (f->esp, arg, 2);
			name = (char *)arg[0];
			initial_size = *(off_t *)arg[1];
			f->eax = filesys_create(name, initial_size);
			break;

		case SYS_REMOVE :                 /* Delete a file. */
			const char *name;
			syscall_get_args (f->esp, arg, 1);
			name = (char *)arg[0];
			f->eax = filesys_remove(name);
			break;

		case SYS_OPEN :                   /* Open a file. */
			open();
			break;

		case SYS_FILESIZE :               /* Obtain a file's size. */
			filesize();
			break;

		case SYS_READ :                   /* Read from a file. */
			read();
			break;

		case SYS_WRITE :                  /* Write to a file. */
			write();
			break;

		case SYS_SEEK :                   /* Change position in a file. */
			seek();
			break;

		case SYS_TELL :                   /* Report current position in a file. */
			tell();
			break;

		case SYS_CLOSE :                  /* Close a file. */
			close();
			break;

    default :
		  break;
	}

  EXIT :
    thread_exit ();
}

void
syscall_get_args (void *esp, int *arg, int count)
{
  for (i = 0; i < count; i++)
	{
		*arg[i] = (esp + 4 * (i + 1));
	}
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
