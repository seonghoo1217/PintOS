#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "userprog/process.h"

/* Project 2: system call */

#define    STDIN_FILENO    0
#define    STDOUT_FILENO    1


void syscall_init(void);

void check_address(void *addr);

void get_argument(void *esp, int *arg, int count);

#endif /* userprog/syscall.h */
