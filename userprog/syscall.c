#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"
#include "threads/palloc.h"

struct lock filesys_lock;
typedef int pid_t;
void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *uaddr);
void exit(int status);

void halt(void);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
pid_t fork(const char *thread_name);
int exec(const char *file);
int wait(int pid);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
                        ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
    lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
    // TODO: Your implementation goes here.
    int syscall_number = f->R.rax; // 원하는 기능에 해당하는 시스템 콜 번호
    switch (syscall_number)
    {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            memcpy(&thread_current()->parent_if,f,sizeof(struct intr_frame));
            f->R.rax = fork(f->R.rdi);
            break;
        case SYS_EXEC:
            f->R.rax = exec(f->R.rdi);
            break;
        case SYS_WAIT:
            f->R.rax = wait(f->R.rdi);
            break;
        case SYS_CREATE:
            f->R.rax = create(f->R.rdi, f->R.rsi);
            break;
        case SYS_REMOVE:
            f->R.rax = remove(f->R.rdi);
            break;
        case SYS_OPEN:
            f->R.rax = open(f->R.rdi);
            break;
        case SYS_FILESIZE:
            f->R.rax = filesize(f->R.rdi);
            break;
        case SYS_READ:
            f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_WRITE:
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi, f->R.rsi);
            break;
        case SYS_TELL:
            f->R.rax = tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
            break;
        default:
            exit(-1);
            break;
    }
    // printf("system call!\n");
    // thread_exit();
}

void check_address(void *uaddr)
{
    struct thread *cur = thread_current();
    if (uaddr == NULL || is_kernel_vaddr(uaddr) || pml4_get_page(cur->pml4, uaddr) == NULL)
    {
        exit(-1);
    }
}

void exit(int status)
{
    struct thread *cur = thread_current();
    cur->exit_status = status;
    printf("%s: exit(%d)\n", thread_name(), status);
    for (int i =2; i < 128; i++){
        if ( thread_current()->fdt[i] != NULL){
            close(i);
        }
    }
    thread_exit();
}

void halt(void)
{
    power_off();
}

bool create(const char *file, unsigned initial_size)
{
    lock_acquire(&filesys_lock);
    check_address(file);
    bool success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
    return success;
}

bool remove(const char *file)
{
    check_address(file);
    return filesys_remove(file);
}

int open(const char *file_name)
{
    check_address(file_name);
    lock_acquire(&filesys_lock);
    struct file *file = filesys_open(file_name);
    if (file == NULL)
    {
        lock_release(&filesys_lock);
        return -1;
    }
    int fd = process_add_file(file);
    if (fd == -1)
        file_close(file);
    lock_release(&filesys_lock);
    return fd;
}

int filesize(int fd)
{
    struct file *file = process_get_file(fd);
    if (file == NULL)
        return -1;
    return file_length(file);
}

void seek(int fd, unsigned position)
{
    struct file *file = process_get_file(fd);
    if (file == NULL)
        return;
    file_seek(file, position);
}

unsigned tell(int fd)
{
    struct file *file = process_get_file(fd);
    if (file == NULL)
        return;
    return file_tell(file);
}

void close(int fd)
{
    struct file *file = process_get_file(fd);
    if (file == NULL)
        return;
    file_close(file);
    process_close_file(fd);
}

int read(int fd, void *buffer, unsigned size)
{
    // check_address(buffer);

    // char *ptr = (char *)buffer;
    // int bytes_read = 0;

    // lock_acquire(&filesys_lock);
    // if (fd == STDIN_FILENO)
    // {
    //     for (int i = 0; i < size; i++)
    //     {
    //         *ptr++ = input_getc();
    //         bytes_read++;
    //     }
    //     lock_release(&filesys_lock);
    // }else if(fd == 1){//STDOUT_FILENO
    //     return -1;
    // }
    // else
    // {
    //     if (fd < 2)
    //     {

    //         lock_release(&filesys_lock);
    //         return -1;
    //     }
    //     struct file *file = process_get_file(fd);
    //     if (file == NULL)
    //     {

    //         lock_release(&filesys_lock);
    //         return -1;
    //     }
    //     bytes_read = file_read(file, buffer, size);
    //     lock_release(&filesys_lock);
    // }
    // return bytes_read;
    check_address(buffer);
    off_t read_byte;
    uint8_t *read_buffer = buffer;
    if (fd == 0) // stdin
    {
        char key;
        for (read_byte = 0; read_byte < size; read_byte++)
        {
            key = input_getc();
            *read_buffer++ = key;
            if (key == '\0')
            {
                break;
            }
        }
    }
    else if (fd == 1) // stdout
    {
        return -1;
    }
    else
    {
        struct file *read_file = process_get_file(fd);
        if (read_file == NULL)
        {
            return -1;
        }
        lock_acquire(&filesys_lock);
        read_byte = file_read(read_file, buffer, size);
        lock_release(&filesys_lock);
    }
    return read_byte;
}

int write(int fd, const void *buffer, unsigned size)
{
    check_address(buffer);
    int bytes_write = 0;
    if (fd == STDOUT_FILENO)
    {
        putbuf(buffer, size);
        bytes_write = size;
    }
    else
    {
        if (fd < 2)
            return -1;
        struct file *file = process_get_file(fd);
        if (file == NULL)
            return -1;
        lock_acquire(&filesys_lock);
        bytes_write = file_write(file, buffer, size);
        lock_release(&filesys_lock);
    }
    return bytes_write;
}

pid_t fork(const char *thread_name)
{
    return process_fork(thread_name, &thread_current()->parent_if);
}

int exec(const char *file)
{
    check_address(file);
    /* process.c 파일의 process_create_initd 함수와 유사하다.
        이 함수에서는 새 스레드를 생성하지 않고 process_exec을 호출한다. */
    /* 커널 메모리 공간에 file의 복사본을 만든다. */
    /* process_exec 함수 안에서 전달 받은 인자를 parsing하는 과정이 있기 때문에 복사본을 만들어서 전달해야 한다. */
    char *file_copy;
    file_copy = palloc_get_page(PAL_ZERO);
    if (file_copy == NULL)
        exit(-1);
    strlcpy(file_copy, file, PGSIZE);

    if (process_exec(file_copy) == -1)
        exit(-1);
}

int wait(int pid)
{
    return process_wait(pid);
}