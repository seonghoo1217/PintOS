#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/init.h" 		// power_off 부르기 위해 사용
#include "filesys/filesys.h" 	// filesys.c 부르기 위해 사용
#include "include/threads/synch.h"
#include "filesys/file.h"
#include "threads/synch.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

#include "userprog/process.h"
typedef int pid_t;
#include "threads/palloc.h"
#include <string.h>
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

	lock_init(&filesys_lock); // Project 2-3: system call
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
			break;
		case SYS_REMOVE:
			remove(f->R.rdi);
			break;		
		case SYS_FORK:
			fork(f->R.rdi, f->R.rsi);
			break;
		case SYS_EXEC:
			if (exec(f->R.rdi) == -1){
				exit(-1);
			}
			break;
		case SYS_WAIT:
			wait(f->R.rdi);
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
		default:
			exit(-1);
			break;
	}
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
	printf("%s: exit(%d)\n", t->name, status); // 정상종료 = status : 0
	thread_exit();
}

/* 파일을 생성하는 시스템 콜 */
bool create (const char *file, unsigned initial_size){
	// file : 생성할 파일의 이름 및 경로 정보
	// initial_size : 생성할 파일의 크기
	lock_acquire(&filesys_lock);
	check_address(file);
	// file 이름에 해당하는 file을 생성하는 함수
	bool x = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return x;
}

/* 파일을 제거하는 시스템 콜 */
bool remove (const char *file){
	check_address(file);
	// file 이름에 해당하는 file을 제거하는 함수
	return filesys_remove(file);
}
/* ---------- FD 필요한 경우 ---------- */

/* fd table에 인자로 들어온 파일 객체를 저장하고 fd를 생성 */
int fdt_add_fd(struct file *file){
	struct thread *curr = thread_current();
	struct file ** fdt = curr-> fdt;

	while (curr->fdt < FDCOUNT_LIMIT && fdt[curr->next_fd]){
		curr->next_fd++;
	}

	if (curr->next_fd >= FDCOUNT_LIMIT){
		return -1;
	}

	fdt[curr->next_fd] = file;
	return curr->next_fd;
}

/* fd table에서 인자로 들어온 fd를 검색하여 찾은 파일 객체를 리턴 */
static struct file * fdt_get_file(int fd){
	if (fd < 0 || fd >= FDCOUNT_LIMIT){
		return NULL;
	}
	struct thread *curr = thread_current();
	return curr->fdt[fd];
}

/* fd table에서 인자로 들어온 fd를 제거 */
static void fdt_remove_fd(int fd){
	struct thread *curr = thread_current();

	if (fd < STDIN_FILENO || fd >= FDCOUNT_LIMIT){
		return;
	}
	curr->fdt[fd] = NULL;
}

/* FILE Descriptor 구현
 * open() 		: 파일을 열 때 사용 
 * filesize() 	: 파일의 크기를 알려줌
 * read() 		: 열린 파일의 데이터를 읽음
 * write() 		: 열린 파일의 데이터를 기록
 * seek()		: 열린 파일의 위치(offset)를 이동
 * tell()		: 열린 파일의 위치(offset)를 알려줌
 * close()		: 열린 파일을 닫음
*/


int open (const char *file){
	check_address(file);
	struct file *target_file = filesys_open(file);

	if (target_file == NULL){
		return -1;
	}
	int fd = fdt_add_fd(target_file);
	
	if (fd == -1){
		file_close(target_file);
	}
	return fd;
}

int filesize (int fd){
	struct file * target_file = fdt_get_file(fd);
	if (target_file == NULL){
		return -1;
	}
	return file_length(target_file);
}


// int read(int fd, void *buffer, unsigned size) {
// 	// 유효한 주소인지부터 체크
// 	check_address(buffer); // 버퍼 시작 주소 체크
// 	unsigned char *buf = buffer;
// 	int read_count;
	
// 	struct file *fileobj = fdt_get_file(fd);

// 	if (fileobj == NULL) {
// 		return -1;
// 	}

// 	/* STDIN일 때: */
// 	if (fd == STDIN_FILENO) {
// 		char key;
// 		for (int read_count = 0; read_count < size; read_count++) {
// 			key  = input_getc();
// 			*buf++ = key;
// 			if (key == '\0') { // 엔터값
// 				break;
// 			}
// 		}
// 	}
// 	/* STDOUT일 때: -1 반환 */
// 	else if (fd == STDOUT_FILENO){
// 		return -1;
// 	}

// 	else {
// 		lock_acquire(&filesys_lock);
// 		read_count = file_read(fileobj, buffer, size); // 파일 읽어들일 동안만 lock 걸어준다.
// 		lock_release(&filesys_lock);

// 	}
// 	return read_count;
// }

int read(int fd, void *buffer, unsigned size)
{
    check_address(buffer);
    off_t read_byte;
    uint8_t *read_buffer = buffer;
    if (fd == 0)
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
    else if (fd == 1)
    {
        return -1;
    }
    else
    {
        struct file *read_file = fdt_get_file(fd);
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

int write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	struct file *fileobj = fdt_get_file(fd);
	int read_count;
	if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		read_count = size;
	}	
	
	else if (fd == STDIN_FILENO) {
		return -1;
	}

	else {
		
		lock_acquire(&filesys_lock);
		read_count = file_write(fileobj, buffer, size);
		lock_release(&filesys_lock);

	}
	return read_count;
}

void seek (int fd, unsigned position){
	struct file *target_file = fdt_get_file(fd);

	if (fd <= STDOUT_FILENO || target_file ==NULL){
		return;
	}
	file_seek(target_file, position);
}

unsigned tell (int fd){
	struct file * target_file = fdt_get_file(fd);

	if (fd <= STDOUT_FILENO || target_file == NULL){
		return;
	}

	file_tell(target_file);
}

void close (int fd){
	struct file *target_file = fdt_get_file(fd);

	if (fd <= STDOUT_FILENO || target_file == NULL || target_file <= 2){
		return;
	}

	fdt_remove_fd(fd);
	file_close(target_file);
}

pid_t fork (const char * thread_name, struct intr_frame *f){
	check_address(thread_name);
	return process_fork(thread_name, f);
}

int wait (tid_t pid){
	return process_wait(pid);
}

int exec(const char *cmd_line){
	check_address(cmd_line);

	int size = strlen(cmd_line) + 1;
	char * fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL){
		exit(-1);
	}
	strlcpy(fn_copy, cmd_line, size);

	if (process_exec(fn_copy) == -1){
		return -1;
	}
	return 0;
}

