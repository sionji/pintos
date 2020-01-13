#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define CLOSE_ALL 0

typedef int mapid_t;

void syscall_init (void);
struct lock filesys_lock;
struct vm_entry *check_address (void *, void *);

void syscall_exit (int exit_status);
int syscall_read (int, void *, unsigned);
int syscall_write (int, void *, unsigned);
int syscall_mmap (int fd, void *addr);
void syscall_munmap (mapid_t);

#endif /* userprog/syscall.h */
