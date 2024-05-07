#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
}

/* --- Project 2: system call --- */

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
    /* 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져오기 */
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
    /*uint64_t arg[7];
    switch (arg[0])
    {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit(arg[1]);
            break;
        case SYS_EXEC:
            exec();
            break;
    }*/
    switch(sys_number) {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            fork(f->R.rdi);
            break;
        case SYS_EXEC:
            exec(f->R.rdi);
            break;
        case SYS_WAIT:
            wait(f->R.rdi);
            break;
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
            write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi, f->R.rsi);
            break;
        case SYS_TELL:
            tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
            break;
    }
    printf("system call!\n");

    thread_exit();
}

void check_address(void *addr) {
    /* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수.
    유저 영역을 벗어난 영역일 경우 프로세스 종료 (exit(-1))*/

    /* --- Project 2: User memory access --- */
    if (!is_user_vaddr(addr) || addr !=NULL||pml4_get_page(t->pml4, addr)== NULL){
        /*
         * pml4_get_page
         * 포인터가 가리키는 주소가 유저 영역 내에 있지만 페이지로 할당하지 않은 영역일 수도 있다.
         * 유저 가상 주소와 대응하는 물리주소를 확인해서 해당 물리 주소와 연결된 커널 가상 주소를 반환하거나 만약 해당 물리 주소가 가상 주소와 매핑되지 않은 영역이면 NULL을 반환
        */
        exit(-1);
    }/*
    if (!(user_addr_start > addr) || !(addr > user_addr_finish)) {
        exit(-1);
        *//* --- Project 2: User memory access --- *//*
    }*/
}

void halt(void){
    power_off();
}

void exit(int status)
{
    struct thread *t = thread_current();
    t->exit_status = status;
    printf("%s: exit%d\n", t->name, status); // Process Termination Message
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

int write (int fd, const void *buffer, unsigned size) {
    if (fd == STDOUT_FILENO)
        putbuf(buffer, size);
    return size;
}

void
putbuf (const char *buffer, size_t n) {
    acquire_console ();
    while (n-- > 0)
        putchar_have_lock (*buffer++);
    release_console ();
}
