#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
struct lock filesys_lock;
void syscall_exit (int exit_status);
int syscall_read (int, void *, unsigned);
int syscall_write (int, void *, unsigned);

#endif /* userprog/syscall.h */
