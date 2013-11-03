/*
 * thread_pool.h
 *
 *  Created on: 2013-11-3
 *      Author: brucewoo
 */

#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <pthread.h>
#include <semaphore.h>

#include "locker.h"
#include "http_connect.h"
#include "queue.h"

struct thread_pool_t
{
	int thread_number;	//线程池中的线程数
	int conn_number;	//请求连接的个数
	int max_resquests;	//请求队列中允许的最大请求数
	pthread_t* threads;	//描述线程池的数组，
	queue_t* conn_head;
	pthread_mutex_t locker;
	sem_t sem;
	bool stop;
};

typedef struct thread_pool_t thread_pool;

void* worker(void* arg)
{
	thread_pool* pool = (thread_pool*)arg;

	while (!pool->stop)
	{
		sem_wait(&pool->sem);
		pthread_mutex_lock(&pool->locker);

		if (queue_empty(pool->conn_head))
		{
			pthread_mutex_unlock(&pool->locker);
			continue;
		}

		queue_t* curr_node = queue_head(pool->conn_head);
		queue_remove(curr_node);
		pthread_mutex_unlock(&pool->locker);
		http_conn* conn = (http_conn *) ((u_char *) curr_node - ((size_t) &((http_conn *)0)->head));
		if (conn == NULL)
		{
			printf("error: http_conn is NULL.");
			continue;
		}
		process(conn);
	}
}

thread_pool* create_thread_pool(int thread_number, int max_requests)
{
	if ((thread_number <= 0) || (max_requests <= 0))
	{
		return NULL;
	}

	thread_pool* pool = (thread_pool *)malloc(sizeof(thread_pool));
	if (pool == NULL)
	{
		return NULL;
	}

	pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_number);
	if (pool->threads == NULL)
	{
		free(pool);
		return NULL;
	}

	pool->conn_number = 0;
	pool->thread_number = thread_number;
	pool->max_resquests = max_requests;
	queue_init(pool->conn_head);

	if (sem_init(&pool->sem, 0, 0) != 0)
	{
		free(pool->threads);
		free(pool);
		return NULL;
	}

	if (pthread_mutex_init(&pool->locker, NULL) != 0)
	{
		sem_destroy(&pool->sem);
		free(pool->threads);
		free(pool);
		return NULL;
	}

	int i=0;
	for (; i<thread_number; ++i)
	{
		printf("create the %dth thread\n", i);
		if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0)
		{
			sem_destroy(&pool->sem);
			pthread_mutex_destroy(&pool->locker);
			free(pool->threads);
			free(pool);
			return NULL;
		}

		if (pthread_detach(pool->threads[i]))
		{
			sem_destroy(&pool->sem);
			pthread_mutex_destroy(&pool->locker);
			free(pool->threads);
			free(pool);
			return NULL;
		}
	}

	return pool;
}

void destroy_thread_pool(thread_pool *pool)
{
	sem_destroy(&pool->sem);
	pthread_mutex_destroy(&pool->locker);
	free(pool->threads);
	free(pool);
	pool->stop = TRUE;
}

bool add_conn(thread_pool *pool, http_conn* conn)
{
	pthread_mutex_lock(&pool->locker);
	if (pool->conn_number > pool->max_resquests)
	{
		pthread_mutex_unlock(&pool->locker);
		return FALSE;
	}

	queue_insert_tail(pool->conn_head, &conn->head);
	pthread_mutex_unlock(&pool->locker);
	sem_post(&pool->sem);
	return TRUE;
}
#endif /* THREAD_POOL_H_ */
