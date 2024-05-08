#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"

#include "threads/flags.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "intrinsic.h"
#include <string.h>

#define	STDIN_FILENO	0
#define	STDOUT_FILENO	1

#define MAX_FD_NUM	(1<<9)

typedef int pid_t;

void check_address(void *addr);
struct file *fd_to_struct_filep(int fd);
int add_file_to_fd_table(struct file *file);
void remove_file_from_fd_table(int fd);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int);
void close (int fd);
bool create (const char *file , unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int read(int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
int filesize(int fd);

void seek(int fd, unsigned position);
unsigned tell (int fd);

void close (int fd);
int exec(char *file_name);
pid_t fork (const char *thread_name, struct intr_frame *f);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
// pid_t fork (const char *thread_name);


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

struct lock filesys_lock;

void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
                        ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    /* -------- project 2-3------------- */
    lock_init(&filesys_lock);
    /* -------- project 2-3------------- */
}


/* --- Project 2: system call --- */

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
    /* 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져와야지 일단 */
    uintptr_t *rsp = f->rsp;
    check_address((void *) rsp);
    int sys_number = f->R.rax; // rax: 시스템 콜 넘버
    /*
    인자 들어오는 순서:
    1번째 인자: %rdi
    2번째 인자: %rsi
    3번째 인자: %rdx
    4번째 인자: %r10
    5번째 인자: %r8
    6번째 인자: %r9
    */


    // TODO: Your implementation goes here.
    switch(sys_number) {
        case (SYS_HALT):
            halt();
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            fork(f->R.rdi, f->R.rsi);
        case SYS_EXEC:
            exec(f->R.rdi);
        case SYS_WAIT:
            wait(f->R.rdi);
        case SYS_CREATE:
            create(f->R.rdi, f->R.rsi);
            break;
        case SYS_REMOVE:
            remove(f->R.rdi);
            break;
        case SYS_OPEN:
            open(f->R.rdi);
            break;
        case SYS_FILESIZE:
            filesize(f->R.rdi);
            break;
        case SYS_READ:
            read(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_WRITE:
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi, f->R.rdx);
            break;
        case SYS_TELL:
            tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
        default:
            thread_exit();
    }
    //thread_exit();
    //printf ("system call!\n");
    //printf("%d", sys_number);
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수.
	유저 영역을 벗어난 영역일 경우 프로세스 종료 (exit(-1))*/
void check_address(void *addr) {
    struct thread *t = thread_current();
    /* --- Project 2: User memory access --- */
    // if (!is_user_vaddr(addr)||addr == NULL)
    //-> 이 경우는 유저 주소 영역 내에서도 할당되지 않는 공간 가리키는 것을 체크하지 않음. 그래서
    // pml4_get_page를 추가해줘야!
    if (!is_user_vaddr(addr)||addr == NULL||
        pml4_get_page(t->pml4, addr)== NULL)
    {
        exit(-1);
    }
}
/* 유저 스택에 있는 인자들을 커널에 저장하는 함수 get_argument()는 x86-64에서는 구현하지 X.*/


/* pintos 종료시키는 함수 */
void halt(void){
    power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status)
{
    struct thread *t = thread_current();
    t->exit_status = status;
    printf("%s: exit(%d)\n", t->name, status); // Process Termination Message
    /* 정상적으로 종료됐다면 status는 0 */
    /* status: 프로그램이 정상적으로 종료됐는지 확인 */
    thread_exit();
}

/* 파일 생성하는 시스템 콜 */
bool create (const char *file, unsigned initial_size) {
    /* 성공이면 true, 실패면 false */
    check_address(file);
    if (filesys_create(file, initial_size)) {
        return true;
    }
    else {
        return false;
    }
}

bool remove (const char *file) {
    check_address(file);
    if (filesys_remove(file)) {
        return true;
    } else {
        return false;
    }
}

/*
부모 복사해서 자식 프로세스 생성하는 함수.
부모: 성공시 자식 pid 반환 / 실패 시 -1
자식: 성공시 0 반환
 */
pid_t fork (const char *thread_name, struct intr_frame *f) {
    return process_fork(thread_name, f);
}


int write (int fd, const void *buffer, unsigned size) {
    check_address(buffer);
    struct file *fileobj = fd_to_struct_filep(fd);
    int read_count;

    lock_acquire(&filesys_lock);
    if (fd == STDOUT_FILENO) {
        putbuf(buffer, size);
        read_count = size;

    }

    else if (fd == STDIN_FILENO) {
        lock_release(&filesys_lock);
        return -1;
    }

    else if (fd >= 2){

        if (fileobj == NULL) {
            lock_release(&filesys_lock);
            exit(-1);
        }

        read_count = file_write(fileobj, buffer, size);

    }
    lock_release(&filesys_lock);
    return read_count;
}


int open (const char *file) {
    check_address(file); // 먼저 주소 유효한지 늘 체크
    struct file *file_obj = filesys_open(file); // 열려고 하는 파일 객체 정보를 filesys_open()으로 받기

    // 제대로 파일 생성됐는지 체크
    if (file_obj == NULL) {
        return -1;
    }
    int fd = add_file_to_fd_table(file_obj); // 만들어진 파일을 스레드 내 fdt 테이블에 추가

    // 만약 파일을 열 수 없으면] -1을 받음
    if (fd == -1) {
        file_close(file_obj);
    }

    return fd;

}
/* 파일을 현재 프로세스의 fdt에 추가 */
int add_file_to_fd_table(struct file *file) {
    struct thread *t = thread_current();
    struct file **fdt = t->file_descriptor_table;
    int fd = t->fdidx; //fd값은 2부터 출발

    while (t->file_descriptor_table[fd] != NULL && fd < FDCOUNT_LIMIT) {
        fd++;
    }

    if (fd >= FDCOUNT_LIMIT) {
        return -1;
    }
    t->fdidx = fd;
    fdt[fd] = file;
    return fd;

}

/*  fd 값을 넣으면 해당 file을 반환하는 함수 */
struct file *fd_to_struct_filep(int fd) {
    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return NULL;
    }

    struct thread *t = thread_current();
    struct file **fdt = t->file_descriptor_table;

    struct file *file = fdt[fd];
    return file;
}

/* file size를 반환하는 함수 */
int filesize(int fd) {
    struct file *fileobj = fd_to_struct_filep(fd);

    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return;
    }

    if (fileobj == NULL) {
        return -1;
    }
    file_length(fileobj);
}

int read(int fd, void *buffer, unsigned size) {
    // 유효한 주소인지부터 체크
    check_address(buffer); // 버퍼 시작 주소 체크
    check_address(buffer + size -1); // 버퍼 끝 주소도 유저 영역 내에 있는지 체크
    unsigned char *buf = buffer;
    int read_count;

    struct file *fileobj = fd_to_struct_filep(fd);

    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return;
    }

    if (fileobj == NULL) {
        return -1;
    }

    /* STDIN일 때: */
    if (fd == STDIN_FILENO) {
        char key;
        for (int read_count = 0; read_count < size; read_count++) {
            key  = input_getc();
            *buf++ = key;
            if (key == '\0') { // 엔터값
                break;
            }
        }
    }
        /* STDOUT일 때: -1 반환 */
    else if (fd == STDOUT_FILENO) {
        return -1;
    }

    else {
        lock_acquire(&filesys_lock);
        read_count = file_read(fileobj, buffer, size); // 파일 읽어들일 동안만 lock 걸어준다.
        lock_release(&filesys_lock);
    }
    return read_count;
}

void seek(int fd, unsigned position) {
    if (fd < 2) {
        return;
    }

    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return;
    }
    struct file *file = fd_to_struct_filep(fd);
    check_address(file);
    if (file == NULL) {
        return;
    }

    file_seek(file, position);
}

unsigned tell (int fd) {
    if (fd <2) {
        return;
    }

    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return;
    }
    struct file *file = fd_to_struct_filep(fd);
    check_address(file);
    if (file == NULL) {
        return;
    }
    return file_tell(fd);

}

void close (int fd) {
    if (fd < 2) {
        return;
    }
    struct file *file = fd_to_struct_filep(fd);
    check_address(file);
    if (file == NULL) {
        return;
    }

    if (fd < 0 || fd >= FDCOUNT_LIMIT) {
        return;
    }
    thread_current()->file_descriptor_table[fd] = NULL;
}

int exec(char *file_name){
    check_address(file_name);

    int size = strlen(file_name) + 1;
    char *fn_copy = palloc_get_page(PAL_ZERO);
    if ((fn_copy) == NULL) {
        exit(-1);
    }
    strlcpy(fn_copy, file_name, size);

    if (process_exec(fn_copy) == -1) {
        return -1;
    }

    NOT_REACHED();
    return 0;

}

int wait (tid_t pid)
{
    process_wait(pid);
}