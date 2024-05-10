#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void close(int fd);

tid_t fork(const char *thread_name, struct intr_frame *f);

#endif /* userprog/syscall.h */