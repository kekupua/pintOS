#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
void process_init(void);
int process_wait (tid_t);
void process_exit (int status);
void process_activate (void);
int process_write(int fd, const void *buffer, unsigned size);

#endif /* userprog/process.h */
