#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "userprog/process.h"

#define    STDIN_FILENO    0
#define    STDOUT_FILENO    1

void syscall_init(void);

void halt(void);

void exit(int status);

bool create(const char *file, unsigned initial_size);

bool remove(const char *file);

int write(int fd, const void *buffer, unsigned size);

void putbuf(const char *buffer, size_t n);

int open(const char *file);

int add_file_to_fd_table(struct file *file);

struct file *fd_to_struct_filep(int fd);

int filesize(int fd);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
