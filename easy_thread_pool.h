/*
 * 线程池操作定义: C接口方式
 */
#ifndef __FREE_EASY_THREAD_POOL_H__
#define __FREE_EASY_THREAD_POOL_H__

/*
 * 线程池句柄
 */
typedef struct __threadpool threadpool_t;

/*
 * 线程任务
 */
typedef struct __threadpool_task
{
    void (*routine)(void *ctx);
    void *context;
}threadpool_task_t;

/*
 * 销毁线程池的回调函数，未执行的任务会作为参数传递给该回调函数
 */
typedef void (*on_threadpool_destroy)(const threadpool_task_t *task);

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 创建一个线程池
 * thread_num：初始创建的线程个数
 * task_size：最大的任务队列个数
 * return：成功返回线程池句柄，失败返回NULL
 */
threadpool_t *threadpool_create(int thread_num, int task_size);

/*
 * 添加任务到线程池
 * pool：threadpool_create()返回的句柄
 * task：待添加的任务
 * priority：task优先级，数值大优先级高
 * return：成功返回0，失败返回-1
 */
int threadpool_schedule(threadpool_t *pool, const threadpool_task_t *task, int priority);

/*
 * 线程池扩容，增加1个线程
 * return：成功返回0，失败返回-1
 */
int threadpool_increase(threadpool_t *pool);

/*
 * 线程池当前线程个数
 * return：返回当前线程数量
 */
int threadpool_threadnum(threadpool_t *pool);

/*
 * 线程池当前任务个数
 * return：返回当前任务数量
 */
int threadpool_tasknum(threadpool_t *pool);

/*
 * 销毁线程池
 * cb：销毁线程池的回调函数，未执行的任务会作为参数传递给该回调函数
 */
void threadpool_destroy(threadpool_t *pool, on_threadpool_destroy cb);

#ifdef __cplusplus
}
#endif

#endif

