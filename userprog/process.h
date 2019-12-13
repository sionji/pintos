#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct thread *find_child (int);
void remove_child_process (struct thread *);
int process_add_file (struct file *);
struct file *process_get_file (int);
void process_close_file (int);

#endif /* userprog/process.h */
