#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void halt(void);

void exit(int status);

bool create(const char *file, unsigned initial_size);

bool remove(const char *file);

int write(int fd, const void *buffer, unsigned size);

void putbuf(const char *buffer, size_t n);

#endif /* userprog/syscall.h */
