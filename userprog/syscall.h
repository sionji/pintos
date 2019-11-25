#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
struct thread * find_child (int tid);

#endif /* userprog/syscall.h */
