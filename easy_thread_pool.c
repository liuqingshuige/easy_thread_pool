/*
 * An easy C style thread pool implement.
 * Copyright FreeCode. All Rights Reserved.
 * MIT License (https://opensource.org/licenses/MIT)
 * 2024 by liuqingshuige
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include "easy_thread_pool.h"

/*
 * 线程任务入口
 */
typedef struct __threadpool_task_entry
{
	int priority; // 优先级，可用来调整任务执行顺序
	threadpool_task_t task;
}threadpool_task_entry_t;

////>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// 互斥锁实现
typedef struct __threadpool_mutex
{
	pthread_mutex_t mutex_;
}mutex_t;

// 互斥锁初始化
static void mutex_init(mutex_t *mtx)
{
	pthread_mutex_init(&mtx->mutex_, NULL);
}

// 销毁
static void mutex_deinit(mutex_t *mtx)
{
	pthread_mutex_destroy(&mtx->mutex_);
}

// 上锁
static void mutex_lock(mutex_t *mtx)
{
	pthread_mutex_lock(&mtx->mutex_);
}

// 解锁
static void mutex_unlock(mutex_t *mtx)
{
	pthread_mutex_unlock(&mtx->mutex_);
}

// 取锁
static pthread_mutex_t *mutex_get(mutex_t *mtx)
{
	return &mtx->mutex_;
}

////>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// 条件变量实现
typedef struct __threadpool_condition
{
	pthread_cond_t cond_;
}condition_t;

// 条件变量初始化
static void condition_init(condition_t *cond)
{
	pthread_cond_init(&cond->cond_, NULL);
}

// 销毁
static void condition_deinit(condition_t *cond)
{
	pthread_cond_destroy(&cond->cond_);
}

// 阻塞等待通知
static void condition_wait(condition_t *cond, mutex_t *mtx)
{
	pthread_cond_wait(&cond->cond_, mutex_get(mtx));
}

// 单个通知
static void condition_signal(condition_t *cond)
{
	pthread_cond_signal(&cond->cond_);
}

// 广播通知
static void condition_broadcast(condition_t *cond)
{
	pthread_cond_broadcast(&cond->cond_);
}

////>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// 队列实现
typedef struct __threadpool_priority_queue
{
	//mutex_t mutex_;
	int capacity_; // 队列容量
	int size_; // 当前队列大小
	threadpool_task_entry_t *array_; // 任务数组
}priority_queue_t;

// 队列初始化
static void priority_queue_init(priority_queue_t *queue, int capacity)
{
	if (capacity <= 0)
		capacity = 64;
	queue->size_ = 0;
	queue->capacity_ = capacity;
	queue->array_ = (threadpool_task_entry_t *)calloc(capacity, sizeof(threadpool_task_entry_t));
	//mutex_init(&queue->mutex_);
}

// 队列销毁
static void priority_queue_deinit(priority_queue_t *queue)
{
	if (queue->array_)
		free(queue->array_);
	queue->array_ = NULL;
	//mutex_deinit(&queue->mutex_);
}

// 当前队列大小
static int priority_queue_size(priority_queue_t *queue)
{
	//mutex_lock(&queue->mutex_);
	int ret = queue->size_;
	//mutex_unlock(&queue->mutex_);
	return ret;
}

// 当前队列是否为空
static int priority_queue_is_empty(priority_queue_t *queue)
{
	return !priority_queue_size(queue);
}

// 取得任务
static threadpool_task_entry_t priority_queue_top(priority_queue_t *queue)
{
	//mutex_lock(&queue->mutex_);
	threadpool_task_entry_t msg = queue->array_[0];
	//mutex_unlock(&queue->mutex_);
	return msg;
}

// 弹出任务：出队
static void priority_queue_pop(priority_queue_t *queue)
{
	//mutex_lock(&queue->mutex_);
	if (queue->size_ > 0)
	{
		memmove(&queue->array_[0], &queue->array_[1], (queue->size_-1)*sizeof(threadpool_task_entry_t));
		queue->size_--;
	}
	//mutex_unlock(&queue->mutex_);
}

// 添加任务：入队
static void priority_queue_push(priority_queue_t *queue, const threadpool_task_entry_t *key)
{
	//mutex_lock(&queue->mutex_);

	if (queue->size_ >= queue->capacity_) // 扩容
	{
		threadpool_task_entry_t *new_arr = (threadpool_task_entry_t *)realloc(queue->array_, (queue->capacity_<<1)*sizeof(threadpool_task_entry_t));
		if (new_arr)
		{
			queue->capacity_ <<= 1;
			queue->array_ = new_arr;
		}
		else
		{
			//mutex_unlock(&queue->mutex_);
			return;
		}
	}

	if (queue->size_ == 0) // 首个消息
	{
		memcpy(&queue->array_[0], key, sizeof(threadpool_task_entry_t));
		queue->size_++;
		//mutex_unlock(&queue->mutex_);
		return;	
	}

	int idx = 0;
	for (idx=0; idx<queue->size_; idx++) // 降序
	{
		if (queue->array_[idx].priority < key->priority)
			break;
	}

	if (idx == queue->size_) // 没有找到，则说明key的优先级最低
	{
		memcpy(&queue->array_[queue->size_], key, sizeof(threadpool_task_entry_t));
		queue->size_++;
	}
	else
	{
		memmove(&queue->array_[idx+1], &queue->array_[idx], (queue->size_-idx)*sizeof(threadpool_task_entry_t));
		memcpy(&queue->array_[idx], key, sizeof(threadpool_task_entry_t));
		queue->size_++;
	}

	//mutex_unlock(&queue->mutex_);
}

////>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
/*
 * 任务队列实现
 */
typedef struct __threadpool_task_queue
{
    priority_queue_t queue_; // 队列：保存任务
    mutex_t mutex_; // 锁
    condition_t cv_;
    int queue_capacity_; // queue_队列容量
	int exit_; // 退出标志
}task_queue_t;

/*
 * 创建一个任务队列
 * capacity：队列最大容量
 * return：成功返回队列句柄，失败返回NULL
 */
static task_queue_t *threadpool_task_queue_create(int capacity)
{
	task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t));
	if (!queue)
		return NULL;

	if (capacity < 0)
		capacity = 64;

	queue->exit_ = 0;
	queue->queue_capacity_ = capacity;

	priority_queue_init(&queue->queue_, capacity);
	mutex_init(&queue->mutex_);
	condition_init(&queue->cv_);

	return queue;
}

/*
 * 销毁队列
 * queue：threadpool_task_queue_create()返回的句柄
 */
static void threadpool_task_queue_destroy(task_queue_t *queue)
{
	condition_deinit(&queue->cv_);
	mutex_deinit(&queue->mutex_);
	priority_queue_deinit(&queue->queue_);
	free(queue);
}

/*
 * 当前队列大小
 * queue：threadpool_task_queue_create()返回的句柄
 */
static int threadpool_task_queue_size(task_queue_t *queue)
{
	mutex_lock(&queue->mutex_);
    int ret = priority_queue_size(&queue->queue_);
    mutex_unlock(&queue->mutex_);
    return ret;
}

/*
 * 添加一个任务到队列
 * queue：队列句柄
 * entry：待添加的任务
 * return：成功1，失败返回0
 */
static int threadpool_task_queue_push(task_queue_t *queue, const threadpool_task_entry_t *entry)
{
	mutex_lock(&queue->mutex_);

	if (priority_queue_size(&queue->queue_) >= queue->queue_capacity_ // 队列已经满了
		|| queue->exit_) // 已经退出
	{
		mutex_unlock(&queue->mutex_);
		return 0;
	}

	priority_queue_push(&queue->queue_, entry);
	condition_signal(&queue->cv_);

	mutex_unlock(&queue->mutex_);

	return 1;
}

// 取出一个任务
static void threadpool_task_queue_get_one(task_queue_t *queue, threadpool_task_entry_t *entry)
{
	*entry = priority_queue_top(&queue->queue_); // 取出任务
	priority_queue_pop(&queue->queue_); // 从队列中删除该任务
}

/*
 * 阻塞获取任务，若当前队列中无任务，则会阻塞等待
 * queue：队列句柄
 * entry：保存任务
 * return：取得任务返回1，否则0
 */
static int threadpool_task_queue_pop(task_queue_t *queue, threadpool_task_entry_t *entry)
{
	mutex_lock(&queue->mutex_);

	while (priority_queue_is_empty(&queue->queue_) && (!queue->exit_))
	{
		condition_wait(&queue->cv_, &queue->mutex_);
	}

	if (queue->exit_) // 已经退出了
	{
		mutex_unlock(&queue->mutex_);
		return 0;
	}

	threadpool_task_queue_get_one(queue, entry);

	mutex_unlock(&queue->mutex_);

	return 1;
}

/*
 * 尝试获取任务，若当前队列中无任务，则立马返回，不会阻塞
 * queue：队列句柄
 * entry：保存任务
 * return：取得任务返回1，否则0
 */
static int threadpool_task_queue_try_pop(task_queue_t *queue, threadpool_task_entry_t *entry)
{
	mutex_lock(&queue->mutex_);

	if (priority_queue_is_empty(&queue->queue_))
	{
		mutex_unlock(&queue->mutex_);
		return 0;
	}
	threadpool_task_queue_get_one(queue, entry);

	mutex_unlock(&queue->mutex_);

	return 1;
}

/*
 * 唤醒并退出队列
 * queue：句柄
 */
static void threadpool_task_queue_wake_exit(task_queue_t *queue)
{
	mutex_lock(&queue->mutex_);
	queue->exit_ = 1;
	condition_broadcast(&queue->cv_);
	mutex_unlock(&queue->mutex_);
}

////>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
/*
 * 线程池
 */
struct __threadpool
{
	task_queue_t *task_queue;
	int thread_num; /* 线程个数 */
	int task_size; /* 线程任务最大个数 */
	pthread_t tid;
	pthread_mutex_t mutex;
	pthread_key_t key;
	pthread_cond_t *terminate_cond;
};

/* 销毁线程池时用到该变量 */
static pthread_t g_exit_tid;



/*
 * 线程处理函数
 */
static void *threadpool_routine(void *param)
{
	threadpool_t *pool = (threadpool_t *)param;
	threadpool_task_entry_t entry;
	
	void (*routine)(void *);
	void *ctx;
	pthread_t tid;
	int ret;
	
	pthread_setspecific(pool->key, pool);
	while (!pool->terminate_cond)
	{
		ret = threadpool_task_queue_pop(pool->task_queue, &entry);
		if (!ret)
			break;
		
		routine = entry.task.routine;
		ctx = entry.task.context;
		
		if (routine)
			routine(ctx);
		
		/* routine()中执行了销毁线程池操作，即调用了threadpool_destroy() */
		if (0 == pool->thread_num)
		{
			free(pool);
			return NULL;
		}
	}
	
	/* 一个线程等待另一个线程退出 */
	pthread_mutex_lock(&pool->mutex);
	
	tid = pool->tid;
	pool->tid = pthread_self();
	
	if (--pool->thread_num == 0) // 最后一个线程，通知执行销毁线程池的线程
		pthread_cond_signal(pool->terminate_cond);

	pthread_mutex_unlock(&pool->mutex);
	
	/* 等待上一个线程退出 */
	if (0 != memcmp(&tid, &g_exit_tid, sizeof(pthread_t)))
		pthread_join(tid, 0);
	
	return NULL;
}

/*
 * 销毁线程池
 * is_in_pool：是否是在任务中(即threadpool_task_t::routine()中)执行该函数
 */
static int threadpool_terminate(threadpool_t *pool, int is_in_pool)
{
	pthread_cond_t terminate_cond = PTHREAD_COND_INITIALIZER;
	
	pthread_mutex_lock(&pool->mutex);
	
	if (pool->terminate_cond) /* 已经有线程执行该函数了 */
	{
		pthread_mutex_unlock(&pool->mutex);
		return -1;
	}
	
	threadpool_task_queue_wake_exit(pool->task_queue); /* 通知所有线程退出 */
	pool->terminate_cond = &terminate_cond;
	
	if (is_in_pool)
	{
		pthread_detach(pthread_self()); /* 分离线程，让系统负责回收该线程资源 */
		pool->thread_num--;
	}

	while (pool->thread_num > 0) /* 等待所有线程退出 */
		pthread_cond_wait(pool->terminate_cond, &pool->mutex);
		
	pthread_mutex_unlock(&pool->mutex);
	
	if (0 != memcmp(&pool->tid, &g_exit_tid, sizeof(pthread_t)))
		pthread_join(pool->tid, 0);

	return 0;
}

/*
 * 是否是线程池中的线程
 */
static int threadpool_is_in_pool(threadpool_t *pool)
{
	return pthread_getspecific(pool->key) == pool;
}

/*
 * 线程创建
 * thread_num：需要创建的线程数量
 */
static int threadpool_create_threads(threadpool_t *pool, int thread_num)
{
	pthread_t tid;
	int ret = 0;
	
	while (pool->thread_num < thread_num)
	{
		ret = pthread_create(&tid, NULL, threadpool_routine, pool);
		if (0 == ret)
			pool->thread_num++;
		else
			break;
	}
	
	if (pool->thread_num == thread_num)
		return pool->thread_num;
	
	threadpool_terminate(pool, 0);
	return -1;
}

/*
 * 创建一个线程池
 * thread_num：初始创建的线程个数
 * task_size：最大的任务队列个数
 * return：成功返回线程池句柄，失败返回NULL
 */
threadpool_t *threadpool_create(int thread_num, int task_size)
{
	threadpool_t *pool;
	int ret = -1;

	pool = (threadpool_t *)calloc(1, sizeof(threadpool_t));
	if (!pool)
		return NULL;

	if (thread_num <= 0)
	{
		thread_num = get_nprocs();
	}

	pool->task_queue = threadpool_task_queue_create(task_size);
	if (pool->task_queue)
	{
		ret = pthread_mutex_init(&pool->mutex, NULL);
		if (0 == ret)
		{
			ret = pthread_key_create(&pool->key, NULL);
			if (0 == ret)
			{
				pool->task_size = task_size;
				pool->thread_num = 0;
				pool->terminate_cond = NULL;

				if (threadpool_create_threads(pool, thread_num) > 0) // 创建线程
					return pool;
				
				pthread_key_delete(pool->key); // 创建线程失败了
			}
			pthread_mutex_destroy(&pool->mutex);
		}
		threadpool_task_queue_destroy(pool->task_queue);
	}

	free(pool);
	return NULL;
}

/*
 * 销毁线程池
 * cb：销毁线程池的回调函数，未执行的任务会作为参数传递给该回调函数
 */
void threadpool_destroy(threadpool_t *pool, on_threadpool_destroy cb)
{
	if (!pool)
		return;

	int is_in_pool = threadpool_is_in_pool(pool);
	threadpool_task_entry_t entry;
	int ret;
	
	ret = threadpool_terminate(pool, is_in_pool);
	if (ret < 0) // 已经有线程执行该函数了
		return;
	
	while (1) // 取出没来得及执行的任务回调给用户
	{
		int res = threadpool_task_queue_try_pop(pool->task_queue, &entry);
		if (!res)
			break;
		
		if (cb)
			cb(&entry.task);
	}

	pthread_key_delete(pool->key);
	pthread_mutex_destroy(&pool->mutex);
	threadpool_task_queue_destroy(pool->task_queue);

	if (!is_in_pool) // 外部线程销毁线程池
		free(pool);
}

/*
 * 线程池扩容，增加1个线程
 * return：成功返回0，失败返回-1
 */
int threadpool_increase(threadpool_t *pool)
{
	if (!pool)
		return -1;

	pthread_t tid;
	int ret;

	pthread_mutex_lock(&pool->mutex);

	ret = pthread_create(&tid, NULL, threadpool_routine, pool);
	if (0 == ret)
		pool->thread_num++;

	pthread_mutex_unlock(&pool->mutex);

	return (0 == ret) ? 0 : -1;
}

/*
 * 线程池当前线程个数
 * return：返回当前线程数量
 */
int threadpool_threadnum(threadpool_t *pool)
{
	if (!pool)
		return 0;

	int ret;

	pthread_mutex_lock(&pool->mutex);

    ret = pool->thread_num;

    pthread_mutex_unlock(&pool->mutex);

    return ret;
}

/*
 * 线程池当前任务个数
 * return：返回当前任务数量
 */
int threadpool_tasknum(threadpool_t *pool)
{
	if (!pool)
		return 0;

	int ret;

	pthread_mutex_lock(&pool->mutex);

    ret = threadpool_task_queue_size(pool->task_queue);

    pthread_mutex_unlock(&pool->mutex);

    return ret;
}

/*
 * 添加任务到线程池
 * pool：threadpool_create()返回的句柄
 * task：待添加的任务
 * return：成功返回0，失败返回-1
 */
int threadpool_schedule(threadpool_t *pool, const threadpool_task_t *task, int priority)
{
	if (!pool || !task)
		return -1;

	int ret = 0;
	threadpool_task_entry_t entry;

	entry.priority = priority;
	entry.task = *task;

    pthread_mutex_lock(&pool->mutex);

	ret = threadpool_task_queue_push(pool->task_queue, &entry);

    pthread_mutex_unlock(&pool->mutex);

	return ret ? 0 : -1;
}





