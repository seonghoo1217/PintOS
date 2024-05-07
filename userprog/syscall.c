#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// filesys.c 불러오기 위해 추가
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt(void);
void exit(int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	/* Project 2-3 */
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져와야함 */
	int sys_num = f->R.rax; // rax : System call Number

	/* 인자 들어오는 순서 (by 범용 레지스터 호출 규약)
	 * 1번째 인자 : %rdi
	 * 2번째 인자 : %rsi
	 * 3번째 인자 : %rdx
	 * 4번째 인자 : %r10
	 * 5번째 인자 : %r8
	 * 6번째 인자 : %r9
	*/
	switch(sys_num){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_CREATE:
			create(f->R.rdi, f->R.rsi);		
		case SYS_REMOVE:
			remove(f->R.rdi);		
		// case SYS_FORK:
		// 	fork(f->R.rdi);
		// case SYS_EXEC:
		// 	exec(f->R.rdi);
		// case SYS_WAIT:
		// 	wait(f->R.rdi);
		// case SYS_OPEN:
		// 	open(f->R.rdi);		
		// case SYS_FILESIZE:
		// 	filesize(f->R.rdi);
		// case SYS_READ:
		// 	read(f->R.rdi, f->R.rsi, f->R.rdx);
		// case SYS_WRITE:
		// 	write(f->R.rdi, f->R.rsi, f->R.rdx);		
		// case SYS_SEEK:
		// 	seek(f->R.rdi, f->R.rsi);		
		// case SYS_TELL:
		// 	tell(f->R.rdi);		
		// case SYS_CLOSE:
		// 	close(f->R.rdi);
		
	}
	printf ("system call!\n");
	printf ("%d", sys_num);
	thread_exit ();
}

/* Project 2-2 */
/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
 * 1. 주소가 NULL 값인지 확인  
 * 2. 인자로 받아온 주소가 유저영역의 주소인지 확인
 * 3. 해당 페이지가 존재하지 않는지 확인 (포인터가 가리키는 주소가 유저 영역내에 있지만 페이지로 할당하지 않은 영역 일수도 있음)
*/
void check_address(void * uaddr){
	struct thread * curr = thread_current();

	if (uaddr == NULL || 
		is_kernel_vaddr(uaddr) || 
		pml4_get_page(curr->pml4, uaddr)== NULL)
		{
		exit(-1);
	}
}

/* Project 2-3 */
/* Pintos 종료시키는 시스템 콜 */
void halt(void){
	power_off();
}

/* 현재 실행중인 프로세스를 종료하는 시스템 콜 */
void exit(int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit %d\n", t->name, status); // 정상종료 = status : 0
	thread_exit();
}

/* 파일을 생성하는 시스템 콜 */
bool create (const char *file, unsigned initial_size){
	// file : 생성할 파일의 이름 및 경로 정보
	// initial_size : 생성할 파일의 크기
	check_address(file);
	// file 이름에 해당하는 file을 생성하는 함수
	return filesys_create(file, initial_size);
}

/* 파일을 제거하는 시스템 콜 */
bool remove (const char *file){
	check_address(file);
	// file 이름에 해당하는 file을 제거하는 함수
	return filesys_remove(file);
}
