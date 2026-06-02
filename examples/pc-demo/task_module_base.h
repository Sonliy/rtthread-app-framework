#ifndef __TASK_MODULE_BASE_H__
#define __TASK_MODULE_BASE_H__

#include "rtos_mock.h"
#include <stdint.h>
#include "task_ctrl.h"

typedef struct msg msg_t;

/* ===== 公共错误码 ===== */
#define APP_EOK        0
#define APP_EINVAL    -1
#define APP_ERROR     -2
#define APP_ETIMEOUT  -3
#define APP_EFULL     -4

/* ===== task_module_t 基类 ===== */
struct task_module;
typedef struct task_module task_module_t;

typedef struct {
    int  (*init)(task_module_t *module);
    int  (*dispatch)(task_module_t *module, const msg_t *msg);
    int  (*transition)(task_module_t *module, uint32_t next_state);
    void (*tick)(task_module_t *module);
} task_module_ops_t;

struct task_module {
    task_cfg_t  task_cfg;
    void       *msg_queue;
    uint16_t    msg_queue_depth;
    uint16_t    msg_size;
    uint32_t    msg_recv_timeout_ms;
    const task_module_ops_t *ops;
    void       *ctx;
};

/* ===== 通用线程入口 ===== */
void module_thread_entry(task_module_t *module);

#endif
