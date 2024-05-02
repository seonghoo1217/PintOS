//
// Created by SHLee on 2024/05/02.
//
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_condvar_thread;
static struct lock lock;
static struct condition condition;

#define CONDITION_STANDARD 26
bool is_condition = false;

void test_priority_condvar(void) {
    int i;

    /* This test does not work with the MLFQS. */
    ASSERT(!thread_mlfqs);

    lock_init(&lock);
    cond_init(&condition);

    thread_set_priority(PRI_MIN);
    for (i = 0; i < 10; i++) {
        int priority = PRI_DEFAULT - (i + 7) % 10 - 1;
        char name[16];
        snprintf(name, sizeof name, "priority %d", priority);
        thread_create(name, priority, priority_condvar_thread, (void *) (uintptr_t) priority);
    }

    // 메인 쓰레드도 기다립니다.
    lock_acquire(&lock);
    while (!is_condition) {
        cond_wait(&condition, &lock);
    }
    lock_release(&lock);
}

static void priority_condvar_thread(void *aux) {
    int priority = thread_get_priority();
    msg("Thread %s starting with priority %d.", thread_name(), priority);
    lock_acquire(&lock);
    if (priority > CONDITION_STANDARD) {
        // 우선순위가 CONDITION_STANDARD보다 높은 스레드
        is_condition = true;
        cond_broadcast(&condition, &lock); // 다른 쓰레드를 깨웁니다.
    }

    while (!is_condition) {
        // 아니면, 기다립니다.
        cond_wait(&condition, &lock);
    }
    msg("Thread %s woke up.", thread_name());
    lock_release(&lock);
}
