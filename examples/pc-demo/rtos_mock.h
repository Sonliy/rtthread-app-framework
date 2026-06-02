/**
 * rtos_mock.h — RT-Thread API 抽象层
 *
 * 用 pthread + 环形缓冲区模拟 RT-Thread 核心 API。
 * 目标：gcc -std=c11 -lpthread -o demo *.c 一键编译运行
 *
 * 模拟范围：
 *   - 消息队列 (rt_mq_init/rt_mq_recv/rt_mq_send_wait)
 *   - 互斥锁   (rt_mutex_create/rt_mutex_take/rt_mutex_release)
 *   - 线程     (rt_thread_create/rt_thread_startup)
 *   - 时间     (rt_tick_get/rt_tick_get_millisecond/rt_thread_mdelay)
 *   - 日志     (rt_kprintf → printf)
 */

#ifndef __RTOS_MOCK_H__
#define __RTOS_MOCK_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* ========================================================================
 *  基础常量
 * ======================================================================== */
#define RT_NULL             NULL
#define RT_EOK              0
#define RT_ERROR            1
#define RT_ETIMEOUT         2
#define RT_EFULL            3
#define RT_WAITING_FOREVER  (-1)
#define RT_IPC_FLAG_PRIO    0x01
#define RT_IPC_FLAG_FIFO    0x00
#define RT_TICK_PER_SECOND  1000

/* ========================================================================
 *  日志
 * ======================================================================== */
typedef int32_t rt_err_t;
typedef int32_t rt_int32_t;

#define rt_kprintf(...)     printf(__VA_ARGS__)

/* ========================================================================
 *  时间
 * ======================================================================== */
static inline uint32_t rt_tick_get(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static inline uint32_t rt_tick_get_millisecond(void) {
    return rt_tick_get();
}

static inline uint32_t rt_tick_from_millisecond(uint32_t ms) {
    return ms;  /* tick = 1ms */
}

#define rt_thread_mdelay(ms)  usleep((ms) * 1000)

/* ========================================================================
 *  互斥锁
 * ======================================================================== */
typedef pthread_mutex_t *rt_mutex_t;

static inline rt_mutex_t rt_mutex_create(const char *name, uint8_t flag) {
    (void)name; (void)flag;
    rt_mutex_t m = malloc(sizeof(pthread_mutex_t));
    if (m) pthread_mutex_init(m, NULL);
    return m;
}

static inline int rt_mutex_take(rt_mutex_t mutex, int32_t timeout) {
    (void)timeout;
    return pthread_mutex_lock(mutex) == 0 ? RT_EOK : RT_ERROR;
}

static inline int rt_mutex_release(rt_mutex_t mutex) {
    return pthread_mutex_unlock(mutex) == 0 ? RT_EOK : RT_ERROR;
}

/* ========================================================================
 *  消息队列（环形缓冲区 + mutex + cond）
 * ======================================================================== */
typedef struct rt_messagequeue {
    uint8_t  *pool;
    uint16_t  msg_size;
    uint16_t  max_msgs;
    uint16_t  head;          /* 读指针（消息索引） */
    uint16_t  tail;          /* 写指针（消息索引） */
    uint16_t  count;         /* 当前消息数 */
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} *rt_mq_t;

static inline int rt_mq_init(rt_mq_t mq, const char *name,
                             void *pool, uint32_t msg_size,
                             uint32_t pool_size, uint8_t flag) {
    (void)name; (void)flag;
    memset(mq, 0, sizeof(*mq));
    mq->pool     = pool;
    mq->msg_size = msg_size;
    mq->max_msgs = pool_size / msg_size;
    pthread_mutex_init(&mq->mutex, NULL);
    pthread_cond_init(&mq->cond, NULL);
    return RT_EOK;
}

static inline int rt_mq_recv(rt_mq_t mq, void *buf, uint32_t size,
                             int32_t timeout_ms) {
    pthread_mutex_lock(&mq->mutex);

    while (mq->count == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&mq->mutex);
            return -RT_ETIMEOUT;
        }
        if (timeout_ms < 0) {
            pthread_cond_wait(&mq->cond, &mq->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            int ret = pthread_cond_timedwait(&mq->cond, &mq->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&mq->mutex);
                return -RT_ETIMEOUT;
            }
        }
    }

    uint8_t *src = mq->pool + (mq->head * mq->msg_size);
    memcpy(buf, src, size < mq->msg_size ? size : mq->msg_size);
    mq->head = (mq->head + 1) % mq->max_msgs;
    mq->count--;
    pthread_mutex_unlock(&mq->mutex);
    return RT_EOK;
}

static inline int rt_mq_send_wait(rt_mq_t mq, const void *buf, uint32_t size,
                                  int32_t timeout_ms) {
    (void)timeout_ms;
    pthread_mutex_lock(&mq->mutex);

    if (mq->count >= mq->max_msgs) {
        pthread_mutex_unlock(&mq->mutex);
        return -RT_EFULL;
    }

    uint8_t *dst = mq->pool + (mq->tail * mq->msg_size);
    memset(dst, 0, mq->msg_size);
    memcpy(dst, buf, size < mq->msg_size ? size : mq->msg_size);
    mq->tail = (mq->tail + 1) % mq->max_msgs;
    mq->count++;
    pthread_cond_signal(&mq->cond);
    pthread_mutex_unlock(&mq->mutex);
    return RT_EOK;
}

/* 非阻塞发送，队列满则立即返回 */
static inline int rt_mq_send(rt_mq_t mq, const void *buf, uint32_t size,
                             int32_t timeout) {
    return rt_mq_send_wait(mq, buf, size, timeout);
}

/* ========================================================================
 *  线程
 * ======================================================================== */
typedef pthread_t rt_thread_t;

typedef struct {
    void       (*entry)(void *param);
    void        *param;
    pthread_t    tid;
} rt_thread_wrapper_t;

static inline void *rt_thread_bootstrap(void *arg) {
    rt_thread_wrapper_t *w = arg;
    w->entry(w->param);
    return NULL;
}

static inline rt_thread_t rt_thread_create(const char *name,
                                           void (*entry)(void *param),
                                           void *param,
                                           uint32_t stack_size,
                                           uint8_t priority,
                                           uint32_t tick) {
    (void)name; (void)stack_size; (void)priority; (void)tick;
    rt_thread_wrapper_t *w = malloc(sizeof(*w));
    w->entry = entry;
    w->param = param;
    pthread_create(&w->tid, NULL, rt_thread_bootstrap, w);
    return w->tid;
}

#define rt_thread_startup(tid)  ((void)0)  /* pthread 已启动 */

#endif /* __RTOS_MOCK_H__ */
