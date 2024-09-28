#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include "easy_thread_pool.h"

void routine1(void *ctx)
{
    printf("taks 1: %d\n", (int)ctx);
}

void routine2(void *ctx)
{
    printf("taks 2: %d\n", (int)ctx);
}

void on_destroy12(const threadpool_task_t *task)
{
    printf("on destroy12 cb: %d\n", (int)task->context);
}

void on_destroy34(const threadpool_task_t *task)
{
    printf("on destroy34 cb\n");
}

void routine3(void *ctx)
{
    printf("taks 3\n");
}

void routine4(void *ctx)
{
    printf("taks 4, destroy pool\n");
    usleep(1000*10);
    threadpool_destroy((threadpool_t *)ctx, on_destroy34);
}


int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("usage:\n\t%s <thread-num> <task-num>\n", argv[0]);
        return -1;
    }

    int thread_num = atoi(argv[1]);
    if (thread_num <= 0)
        thread_num = get_nprocs();
    printf("thread_num: %d\n", thread_num);
    int task_size = atoi(argv[2]);
    threadpool_task_t task1, task2;
    threadpool_task_t task3, task4;

    task1.routine = routine1;
    task2.routine = routine2;

    /* destroy NOT in pool */
    threadpool_t *pool1 = threadpool_create(thread_num, task_size);
    task1.context = (void *)1;
    threadpool_schedule(pool1, &task1, 1);
    task1.context = (void *)11;
    threadpool_schedule(pool1, &task1, 1);

    task2.context = (void *)2;
    threadpool_schedule(pool1, &task2, 2);
    task2.context = (void *)22;
    threadpool_schedule(pool1, &task2, 2);

    task1.context = (void *)111;
    threadpool_schedule(pool1, &task1, 1);
    task1.context = (void *)11111;
    threadpool_schedule(pool1, &task1, 1);

    task2.context = (void *)222;
    threadpool_schedule(pool1, &task2, 2);
    task2.context = (void *)2222;
    threadpool_schedule(pool1, &task2, 2);
    usleep(1);
    threadpool_destroy(pool1, on_destroy12);

    /* destroy in pool */
    threadpool_t *pool2 = threadpool_create(thread_num, task_size);

    task3.routine = routine3;
    task4.routine = routine4;
    task4.context = pool2;

    threadpool_schedule(pool2, &task3, 1);
    threadpool_schedule(pool2, &task4, 2);
    sleep(1);

    return 0;
}



