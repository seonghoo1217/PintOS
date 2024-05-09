#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* --------- Project 1 ----------*/

/* sleep list: thread blocked 상태의 스레드를 관리하기 위한 리스트 자료구조 */
static struct list sleep_list;
/* next tick to awake: 슬립 리스트에서 대기 중인 스레드들의 wakeup_tick 값 중 최솟값을 저장 */
static int64_t next_tick_to_awake;

/* --------- Project 1 ----------*/

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;




/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

static long long next_tick_to_awake;


/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
    ASSERT (intr_get_level () == INTR_OFF);

    /* Reload the temporal gdt for the kernel
     * This gdt does not include the user context.
     * The kernel will rebuild the gdt with user context, in gdt_init (). */
    struct desc_ptr gdt_ds = {
            .size = sizeof (gdt) - 1,
            .address = (uint64_t) gdt
    };
    lgdt (&gdt_ds);

    /* Init the globla thread context */
    lock_init (&tid_lock);
    list_init (&ready_list);
    list_init (&destruction_req);

    /* ------ Project 1 -------- */
    /* 슬립 큐 & next tick to awake 초기화 코드 추가 */
    list_init (&sleep_list);
    next_tick_to_awake = INT64_MAX; // 최솟값 찾아가야 하니 초기화할 때는 정수 최댓값.
    /* ------ Project 1 -------- */

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
        else if (t->pml4 != NULL)
		user_ticks++;
#endif
    else
        kernel_ticks++;

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */


tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) {
    struct thread *t;
    struct thread *curr = thread_current();
    tid_t tid;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();
    /* 현재 스레드의 자식 리스트에 새로 생성한 스레드 추가 */
    list_push_back(&curr->child_list, &t->child_elem);

    /* Call the kernel_thread if it scheduled.
     * Note) rdi is 1st argument, and rsi is 2nd argument. */
    t->tf.rip = (uintptr_t) kernel_thread;
    t->tf.R.rdi = (uint64_t) function;
    t->tf.R.rsi = (uint64_t) aux;
    t->tf.ds = SEL_KDSEG;
    t->tf.es = SEL_KDSEG;
    t->tf.ss = SEL_KDSEG;
    t->tf.cs = SEL_KCSEG;
    t->tf.eflags = FLAG_IF;

    /* project 2-3: system call */
    t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES); // fdt 공간 할당
    if (t->fdt == NULL){
        return TID_ERROR;
    }
    t->next_fd = 2;
    t->fdt[0] = 1;
    t->fdt[1] = 2;
    

    /* Add to run queue. */
    thread_unblock (t);
    /* --- pjt 1.2 --- */
    if (cmp_priority(&t->elem, &curr->elem, NULL)) {
        thread_yield();
    }
    /*
    thread unblock 후 현재 실행중인 thread와 우선순위 비교해서
    새로 생성된 thread 우선순위 높으면 thread_yield() 통해 cpu 양보
    */

    return tid;
}

/* thread_sleep: thread_yield를 기반으로 만든다. */
void thread_sleep(int64_t ticks) {
    struct thread *curr = thread_current();
    enum intr_level old_level;

    /* Thread를 blocked 상태로 만들고 sleep queue에 삽입해서 대기 */
    ASSERT (!intr_context ()); // 외부 인터럽트 프로세스 수행 중이면 True, 아니면 False
    //ASSERT (intr_get_level () == INTR_OFF); 얘는 여기서 필요 없음. 이건

    old_level = intr_disable();

    curr->wakeup_tick = ticks; // 현재 스레드를 깨우는 tick은 인자로 받은 tick만큼의 시간이 지나고서!
    /* thread를 sleep queue에 삽입 */
    if (curr != idle_thread) { // idle_thread: 유휴 스레드. running하지 않는.
        list_push_back (&sleep_list, &thread_current()->elem);
    }

    update_next_tick_to_awake(ticks); // 다음 깨워야 할 스레드가 바뀔 가능성이 있으니 최소 틱을 저장.
    do_schedule(THREAD_BLOCKED); // context switch: 이번에는 blocked(sleep) 상태로 바꿔줘.
    intr_set_level(old_level);
}



void thread_awake(int64_t ticks) {
    /* Sleep queue에서 깨워야 할 thread를 찾아서 wakeup */
    next_tick_to_awake = INT64_MAX;
    struct list_elem *e = list_begin(&sleep_list); // sleep list의 첫번째 elem
    struct thread *t;

    /* 역할 1: sleep_list의 스레드를 모두 순회하면서 wakeup_tick이 ticks보다 작으면 깨워야!*/
    for (e; e != list_end(&sleep_list);)
    {
        t = list_entry(e, struct thread, elem); // sleep list에 들어있는 엔트리 중에서 계속 돌아.
        if (t->wakeup_tick <= ticks) // 깨워달라 한 시간(wakeup_tick)보다 지금 시간(ticks)이 지났다면
            // 이때 지금 시간(ticks)은 thread_awake()를 실행하기 직전의 next_tick_to_awake.
        {
            e = list_remove(&t->elem); // 얘네들은 다 깨워야지. 현재 t는 sleep_list에 연결된 스레드.
            thread_unblock(t); // 리스트에서 지우고 난 다음 unblock 상태로 변경!
            // 여기서는 list_remove에서 이미 다음 리스트로 연결해주니 list_next가 필요 X.
        }
            /* 역할 2: 위에서 thread가 일어났다면 sleep_list가 변했으니 next_tick_to_awake가 바뀜.
            현재 ticks에서 일어나지 않은 스레드 중 가장 작은 wakeup_tick이 next_tick_to_awake. 얘를 갱신.
            즉, 아직 깰 시간이 안 된 애들 중 가장 먼저 일어나야 하는 애를 next_tick_to_awake!*/
        else {
            update_next_tick_to_awake(t->wakeup_tick);
            e= list_next(e); //위에서는 애를 리스트에서 지우니까 for문 돌고 나면 알아서 갱신. 여기는 그게 아니니 list_next로 갱신.
        }
    }
}

/* ticks와 next_tick_to_awake를 비교해 작은 값을 넣는다. */
void update_next_tick_to_awake(int64_t ticks) {
    next_tick_to_awake = MIN(next_tick_to_awake, ticks);
}

int64_t get_next_tick_to_awake(void) {
    return next_tick_to_awake;
}




/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);
    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable (); // interreput disable -> 인터럽트 끈다.
    ASSERT (t->status == THREAD_BLOCKED);

    /* --- Project 1.2 --- */
    // list_push_back (&ready_list, &t->elem); 맨 뒤로 넣을 필요 없으니 삭제
    list_insert_ordered(&ready_list, &t->elem, &cmp_priority, NULL);

    t->status = THREAD_READY;
    intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
    return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
    struct thread *t = running_thread ();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Just set our status to dying and schedule another process.
       We will be destroyed during the call to schedule_tail(). */
    intr_disable ();
    do_schedule (THREAD_DYING);
    NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */

/* 현재 running 중인 스레드를 비활성화 시키고 ready_list에 삽입.*/
void
thread_yield (void) {
    struct thread *curr = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable (); // 인터럽트를 비활성하고 이전 인터럽트 상태(old_level)를 받아온다. 조교님 설명 체크
    if (curr != idle_thread) // 현재 스레드가 노는 스레드가 아니면
        list_insert_ordered(&ready_list, &curr->elem, &cmp_priority, NULL); // 우선순위대로 ready queue에 배정
    //list_push_back (&ready_list, &curr->elem); ready리스트 맨 마지막에 curr을 줄세워.
    do_schedule (THREAD_READY); // context switch 작업 수행 - running인 스레드를 ready로 전환.
    intr_set_level (old_level); // 인자로 전달된 인터럽트 상태로 인터럽트 설정하고 이전 인터럽트 상태 반환
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
    thread_current()->init_priority = new_priority;
    /* --- Pjt 1.2 --- */
    refresh_priority();
    max_priority();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
    return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
    /* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
    /* TODO: Your implementation goes here */
    return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
    struct semaphore *idle_started = idle_started_;

    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;) {
        /* Let someone else run. */
        intr_disable ();
        thread_block ();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
    ASSERT (function != NULL);

    intr_enable ();       /* The scheduler runs with interrupts off. */
    function (aux);       /* Execute the thread function. */
    thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
    t->priority = priority;
    t->magic = THREAD_MAGIC;

    /* Project 1: donation */
    t->init_priority = priority;
    list_init(&t->donations);
    t->wait_on_lock =NULL;

    /* Project 2: system call */
    t->exit_status = 0;
    t->running = NULL;              // 실행중인 파일이 없다

    list_init(&t->child_list);
    sema_init(&t->wait_sema, 0);
    sema_init(&t->fork_sema, 0);
    sema_init(&t->free_sema, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
    if (list_empty (&ready_list))
        return idle_thread;
    else
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
    __asm __volatile(
            "movq %0, %%rsp\n"
            "movq 0(%%rsp),%%r15\n"
            "movq 8(%%rsp),%%r14\n"
            "movq 16(%%rsp),%%r13\n"
            "movq 24(%%rsp),%%r12\n"
            "movq 32(%%rsp),%%r11\n"
            "movq 40(%%rsp),%%r10\n"
            "movq 48(%%rsp),%%r9\n"
            "movq 56(%%rsp),%%r8\n"
            "movq 64(%%rsp),%%rsi\n"
            "movq 72(%%rsp),%%rdi\n"
            "movq 80(%%rsp),%%rbp\n"
            "movq 88(%%rsp),%%rdx\n"
            "movq 96(%%rsp),%%rcx\n"
            "movq 104(%%rsp),%%rbx\n"
            "movq 112(%%rsp),%%rax\n"
            "addq $120,%%rsp\n"
            "movw 8(%%rsp),%%ds\n"
            "movw (%%rsp),%%es\n"
            "addq $32, %%rsp\n"
            "iretq"
            : : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
    uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
    uint64_t tf = (uint64_t) &th->tf;
    ASSERT (intr_get_level () == INTR_OFF);

    /* The main switching logic.
     * We first restore the whole execution context into the intr_frame
     * and then switching to the next thread by calling do_iret.
     * Note that, we SHOULD NOT use any stack from here
     * until switching is done. */
    __asm __volatile (
        /* Store registers that will be used. */
            "push %%rax\n"
            "push %%rbx\n"
            "push %%rcx\n"
            /* Fetch input once */
            "movq %0, %%rax\n"
            "movq %1, %%rcx\n"
            "movq %%r15, 0(%%rax)\n"
            "movq %%r14, 8(%%rax)\n"
            "movq %%r13, 16(%%rax)\n"
            "movq %%r12, 24(%%rax)\n"
            "movq %%r11, 32(%%rax)\n"
            "movq %%r10, 40(%%rax)\n"
            "movq %%r9, 48(%%rax)\n"
            "movq %%r8, 56(%%rax)\n"
            "movq %%rsi, 64(%%rax)\n"
            "movq %%rdi, 72(%%rax)\n"
            "movq %%rbp, 80(%%rax)\n"
            "movq %%rdx, 88(%%rax)\n"
            "pop %%rbx\n"              // Saved rcx
            "movq %%rbx, 96(%%rax)\n"
            "pop %%rbx\n"              // Saved rbx
            "movq %%rbx, 104(%%rax)\n"
            "pop %%rbx\n"              // Saved rax
            "movq %%rbx, 112(%%rax)\n"
            "addq $120, %%rax\n"
            "movw %%es, (%%rax)\n"
            "movw %%ds, 8(%%rax)\n"
            "addq $32, %%rax\n"
            "call __next\n"         // read the current rip.
            "__next:\n"
            "pop %%rbx\n"
            "addq $(out_iret -  __next), %%rbx\n"
            "movq %%rbx, 0(%%rax)\n" // rip
            "movw %%cs, 8(%%rax)\n"  // cs
            "pushfq\n"
            "popq %%rbx\n"
            "mov %%rbx, 16(%%rax)\n" // eflags
            "mov %%rsp, 24(%%rax)\n" // rsp
            "movw %%ss, 32(%%rax)\n"
            "mov %%rcx, %%rdi\n"
            "call do_iret\n"
            "out_iret:\n"
            : : "g"(tf_cur), "g" (tf) : "memory"
            );
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) { // 현재 running 중인 스레드를 status로 바꾸고 새로운 스레드를 실행.
    ASSERT (intr_get_level () == INTR_OFF); //현재 인터럽트가 없어야 하고
    ASSERT (thread_current()->status == THREAD_RUNNING); // 현재 스레드 상태가 running일 때 실행.
    while (!list_empty (&destruction_req)) {
        struct thread *victim =
        list_entry (list_pop_front (&destruction_req), struct thread, elem);
        palloc_free_page(victim);
    }
    thread_current ()->status = status;
    schedule ();
}

static void
schedule (void) { // running인 스레드를 빼내고 next 스레드를 running으로 만든다.
    struct thread *curr = running_thread ();
    struct thread *next = next_thread_to_run ();

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (curr->status != THREAD_RUNNING);
    ASSERT (is_thread (next));
    /* Mark us as running. */
    next->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
	process_activate (next);
#endif

    if (curr != next) {
        /* If the thread we switched from is dying, destroy its struct
           thread. This must happen late so that thread_exit() doesn't
           pull out the rug under itself.
           We just queuing the page free reqeust here because the page is
           currently used bye the stack.
           The real destruction logic will be called at the beginning of the
           schedule(). */
        if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
            ASSERT (curr != next);
            list_push_back (&destruction_req, &curr->elem);
        }

        /* Before switching the thread, we first save the information
         * of current running. */
        thread_launch (next);
    }
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* --- Project 1-2. Priority Scheduling --- */

/* t_a와 t_b의 우선순위를 비교해 더 높은 애면 1을 반환*/
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread* t_a;
    struct thread* t_b;

    t_a = list_entry(a, struct thread, elem);
    t_b = list_entry(b, struct thread, elem);
    return ((t_a->priority) > (t_b->priority)) ? true : false;
}


void max_priority (void){
    if (list_empty(&ready_list)) {
        return;
    }

    int run_priority = thread_current()->priority;
    struct list_elem *e = list_begin(&ready_list);
    struct thread *t = list_entry(e, struct thread, elem);

    if (t->priority > run_priority) {
        thread_yield();
    }
}

/* --- Project 1-2. Priority Scheduling --- */

// Donate
// nested Donation 방식
void donate_priority (void)
{
    int cnt = 0;
    struct thread *t = thread_current();
    int cur_priority = t->priority;

    while ( cnt < 9 )
    {
        cnt++;
        if (t->wait_on_lock == NULL)
        {
            break;
        }

        t = t->wait_on_lock->holder;
        t->priority = cur_priority;
    }
}

// 만약 lock의 holder가 원하는 lock이 없다면 nested donation을 중지해야한다 (IF문).
//원하는 lock이 있다면 lock의 holder에게도 current thread의 priority를 준다.

void remove_with_lock(struct lock *lock) {
    struct thread *cur = thread_current();
    struct list_elem *e;

    for (e= list_begin(&cur->donations); e != list_end(&cur->donations);e = list_next(e)){
        struct thread *t = list_entry(e, struct thread, donation_elem);
        if (t->wait_on_lock == lock) {
            list_remove(&t->donation_elem);
        }
    }
}

//thread *t 는 현재 running중인 thread를 말한다.
// donations를 순회한다. 만약 donation_elem의 주인 thread가 이제 해제한 lock이면 그 elem을 리스트에서 제외한다.

//lock이 해제되었을 때, priority를 갱신하는 작업이 필요하다. 그것이 refresh_priority()이다.
// (lock_release에서 remove_with_lock()을 실행하고 바로 refresh_priority()를 실행했던 것을 기억하자.


void refresh_priority(void) {
    struct thread *cur = thread_current();
    cur->priority = cur->init_priority;

    if (!list_empty(&cur->donations)) {
        list_sort(&cur->donations, thread_compare_donate_priority, 0);
        struct thread *front = list_entry (list_front(&cur->donations), struct thread, donation_elem);
        if (front->priority > cur->priority)
        {
            cur->priority = front->priority;
        }
    }
}
//lock이 해제되었을 때, 여전히 thread_current (=running_thread)가 다른 lock을 들고 있어서 donation을 받을 수 있다면,
//donations에서 priority 순으로 정렬하고, 그 중 가장 높은 prioirty를 받아온다.

/* --- project 1.4 --- */
bool thread_compare_donate_priority(const struct list_elem *l, const struct list_elem *s, void *aux) {
    return list_entry (l, struct thread, donation_elem)->priority > list_entry (s, struct thread, donation_elem)->priority;
}