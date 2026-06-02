#ifndef __TASK_SYNC_MANAGER_H__
#define __TASK_SYNC_MANAGER_H__

#include "rtos_mock.h"
#include "task_ctrl.h"

#define SYNC_TTL_DEFAULT_MS      5000
#define SYNC_INTERVAL_DEFAULT_MS 1000
#define SYNC_TEMP_QUEUE_MAX      64

int  sync_manager_start(uint32_t ttl_ms, uint32_t interval_ms);
int  sync_manager_purge(void);
int  sync_manager_get_purge_count(module_id_e id);

#endif
