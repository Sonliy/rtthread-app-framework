#include "task_msg_bus.h"
#include "task_module_base.h"
#include <string.h>

typedef struct {
    task_module_t *module_table[MSG_BUS_MAX_MODULES];
    rt_mutex_t     bus_mutex;
} msg_bus_ctrl_t;

static msg_bus_ctrl_t g_msg_bus;

int msg_bus_init(void) {
    memset(&g_msg_bus, 0, sizeof(g_msg_bus));
    g_msg_bus.bus_mutex = rt_mutex_create("msg_bus", RT_IPC_FLAG_PRIO);
    if (g_msg_bus.bus_mutex == RT_NULL) return APP_ERROR;
    return APP_EOK;
}

int msg_bus_register_module(task_module_t *module) {
    if (module == RT_NULL || module->task_cfg.id <= MODULE_ID_UNKNOWN ||
        module->task_cfg.id >= MODULE_ID_MAX)
        return APP_EINVAL;

    rt_mutex_take(g_msg_bus.bus_mutex, RT_WAITING_FOREVER);
    g_msg_bus.module_table[module->task_cfg.id] = module;
    rt_mutex_release(g_msg_bus.bus_mutex);
    return APP_EOK;
}

int msg_bus_send(module_id_e src, module_id_e dest,
                 uint32_t msg_id, msg_prio_e prio,
                 const void *data, uint16_t data_len,
                 rt_int32_t timeout) {
    task_module_t *target;
    msg_t msg;
    rt_err_t ret;

    if (dest <= MODULE_ID_UNKNOWN || dest >= MODULE_ID_MAX) return APP_EINVAL;
    if (data_len > sizeof(msg.data_buf)) return APP_EFULL;

    rt_mutex_take(g_msg_bus.bus_mutex, RT_WAITING_FOREVER);
    target = g_msg_bus.module_table[dest];
    rt_mutex_release(g_msg_bus.bus_mutex);

    if (target == RT_NULL || target->msg_queue == RT_NULL) return APP_ERROR;

    memset(&msg, 0, sizeof(msg));
    msg.msg_id      = msg_id;
    msg.src_module  = src;
    msg.dest_module = dest;
    msg.msg_prio    = prio;
    msg.data_len    = data_len;
    msg.timestamp   = rt_tick_get();

    if (data != RT_NULL && data_len > 0)
        memcpy(msg.data_buf, data, data_len);

    ret = rt_mq_send_wait((rt_mq_t)target->msg_queue, &msg, sizeof(msg), timeout);
    if (ret == -RT_ETIMEOUT) return APP_ETIMEOUT;
    if (ret != RT_EOK)       return APP_ERROR;
    return APP_EOK;
}

task_module_t *msg_bus_get_module(module_id_e id) {
    if (id <= MODULE_ID_UNKNOWN || id >= MODULE_ID_MAX) return RT_NULL;
    task_module_t *module;
    rt_mutex_take(g_msg_bus.bus_mutex, RT_WAITING_FOREVER);
    module = g_msg_bus.module_table[id];
    rt_mutex_release(g_msg_bus.bus_mutex);
    return module;
}
