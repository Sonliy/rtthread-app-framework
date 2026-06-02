#ifndef __TASK_CTRL_H__
#define __TASK_CTRL_H__

#include <stdint.h>

typedef enum {
    MODULE_ID_UNKNOWN = 0,
    MODULE_ID_TASK_MGR,
    MODULE_ID_MSG_BUS,
    MODULE_ID_STATE_MGR,
    MODULE_ID_LED,
    MODULE_ID_COM_PC,
    MODULE_ID_SYNC_MGR,
    MODULE_ID_MAX,
} module_id_e;

typedef enum {
    TASK_STATE_UNINIT = -1,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_SUSPENDED,
    TASK_STATE_ERROR
} task_state_e;

typedef enum {
    TASK_PRIO_LOW = -1,
    TASK_PRIO_MID,
    TASK_PRIO_HIGH,
    TASK_PRIO_HIGHEST
} task_prio_e;

typedef struct {
    module_id_e id;
    const char *name;
    char        task_name[32];
    void      (*task_entry)(void *);
    task_prio_e prio;
    uint32_t    stack_size;
    void       *task_param;
    task_state_e curr_state;
} task_cfg_t;

#endif
