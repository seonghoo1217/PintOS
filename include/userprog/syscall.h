#include "lib/stdio.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void halt(void);
void exit(int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
// struct lock filesys_lock;


#endif /* userprog/syscall.h */
