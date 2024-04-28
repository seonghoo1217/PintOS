#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks; 

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
/* 이 함수는 시스템의 타이머를 설정하고 인터럽트를 등록하는 역할 */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 루프당틱(loop_per_tick)을 보정하여 간단한 지연을 구현 */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* One timer tick보다 작으면서 가장 큰 2의 거듭제곱인 값으로 loops_per_tick을 대략적으로 설정 */
	loops_per_tick = 1u << 10; // loops_per_tick 변수를 2의 10제곱 (즉, 1024)으로 설정
							   // 1u << 10은 1을 왼쪽으로 10비트 이동하여 1024를 나타냄
	while (!too_many_loops (loops_per_tick << 1)) { // loops_per_tick을 두 배로 증가시키고, 이를 too_many_loops 함수에 전달하여 해당 함수가 반환하는 값이 false일 때까지 반복
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ); // loops_per_tick * TIMER_FREQ : 초당 반복 횟수
}

/* 운영 체제 부팅 이후에 발생한 타이머 틱의 수를 반환하는 함수나 변수에 대한 설명 */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable (); // 인터럽트 레벨을 저장하고, 인터럽트를 비활성
	int64_t t = ticks;							 // 전역변수 ticks의 값(OS가 부팅되고서 흐르는 틱)을 가져와 변수 t에 저장
	intr_set_level (old_level);					 // 인터럽트 레벨을 복원 -> 인터럽트 활성화
	barrier ();
	return t;									 // 저장한 타이머 틱 수 반환
}

/* 이 함수는 timer_ticks() 함수를 통해 얻은 시간을 기준으로 현재까지 경과한 시간을 계산하여 반환 */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* timer_sleep 함수는 주어진 타이머 틱 수 동안 실행을 중지하고, 이 기간 동안 다른 스레드가 실행될 수 있도록 함 */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks (); // timer_ticks() 함수를 사용하여 현재 타이머 틱 수를 가져와 초기화

	// ASSERT (intr_get_level () == INTR_ON);
	// while (timer_elapsed (start) < ticks)
	// 	thread_yield (); // 다른 스레드가 실행될 기회를 줍니다
	thread_sleep(start + ticks);// 새롭게 추가
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	thread_awake(ticks); // 새롭게 추가 
}

/* 이 함수는 LOOPS 반복이 하나의 타이머 틱보다 오래 걸리는지 여부를 확인하고, 
   그렇다면 true를 반환하고, 그렇지 않으면 false를 반환 */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 이 함수는 대략적으로 주어진 시간(초)만큼 실행을 중지하는 역할 */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
