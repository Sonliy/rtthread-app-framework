#ifndef __TASK_MSG_BUS_H__
#define __TASK_MSG_BUS_H__

#include "rtos_mock.h"
#include "task_ctrl.h"

typedef struct task_module task_module_t;

typedef enum {
    MSG_PRIO_LOW = 0,
    MSG_PRIO_MID,
    MSG_PRIO_HIGH,
    MSG_PRIO_EMERGENCY
} msg_prio_e;

/* ===== 系统级公共消息 ID ===== */
typedef enum {
    MSG_ID_NONE = 0,
    MSG_ID_MODULE_INIT,
    MSG_ID_MODULE_START,
    MSG_ID_MODULE_STOP,
    MSG_ID_MODULE_TIMER,
    MSG_ID_MODULE_RESET,
    MSG_ID_SYSTEM_NORMAL,
    MSG_ID_SYSTEM_ALARM,
    MSG_ID_SYSTEM_FAULT,
} msg_id_e;

/* ===== 消息结构体 ===== */
typedef struct msg {
    uint32_t    msg_id;
    module_id_e src_module;
    module_id_e dest_module;
    msg_prio_e  msg_prio;
    uint16_t    data_len;
    uint8_t     data_buf[64];
    uint32_t    timestamp;
} msg_t;

#define MSG_BUS_MAX_MODULES MODULE_ID_MAX

/* ===== 消息总线 ===== */
int msg_bus_init(void);
int msg_bus_register_module(task_module_t *module);
int msg_bus_send(module_id_e src, module_id_e dest,
                 uint32_t msg_id, msg_prio_e prio,
                 const void *data, uint16_t data_len,
                 rt_int32_t timeout);

task_module_t *msg_bus_get_module(module_id_e id);

#endif
