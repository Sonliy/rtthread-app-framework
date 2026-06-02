#include "task_sync_manager.h"
#include "task_module_base.h"
#include "task_msg_bus.h"
#include <string.h>

#define MSG_BUS_MAX_MODULES MODULE_ID_MAX

typedef struct {
    uint32_t   ttl_ms;
    uint32_t   interval_ms;
    uint32_t   total_purged;
    uint32_t   purge_count[MODULE_ID_MAX];
    rt_thread_t thread;
} sync_mgr_t;

static sync_mgr_t g_sync_mgr;

int sync_manager_purge(void) {
    msg_t temp[SYNC_TEMP_QUEUE_MAX];
    uint32_t now = rt_tick_get();
    int total_purged = 0;

    for (int id = MODULE_ID_UNKNOWN + 1; id < MODULE_ID_MAX; id++) {
        task_module_t *module = msg_bus_get_module((module_id_e)id);

        if (module == RT_NULL || module->msg_queue == RT_NULL)
            continue;

        int count = 0;
        while (count < SYNC_TEMP_QUEUE_MAX) {
            rt_err_t ret = rt_mq_recv((rt_mq_t)module->msg_queue,
                                      &temp[count], sizeof(msg_t), 0);
            if (ret != RT_EOK) break;
            count++;
        }

        int fresh = 0;
        for (int i = 0; i < count; i++) {
            uint32_t age_ms = (now - temp[i].timestamp)
                              * (1000 / RT_TICK_PER_SECOND);
            if (age_ms < g_sync_mgr.ttl_ms) {
                rt_mq_send((rt_mq_t)module->msg_queue,
                           &temp[i], sizeof(msg_t), 0);
                fresh++;
            } else {
                total_purged++;
            }
        }

        if (count - fresh > 0) {
            g_sync_mgr.purge_count[id] += (count - fresh);
            rt_kprintf("[SyncMgr] %s: %d stale purged (%d fresh)\n",
                       module->task_cfg.name, count - fresh, fresh);
        }
    }

    g_sync_mgr.total_purged += total_purged;
    return total_purged;
}

static void sync_manager_thread_entry(void *param) {
    (void)param;
    rt_kprintf("[SyncMgr] started, ttl=%dms interval=%dms\n",
               g_sync_mgr.ttl_ms, g_sync_mgr.interval_ms);

    while (1) {
        rt_thread_mdelay(g_sync_mgr.interval_ms);
        sync_manager_purge();
    }
}

int sync_manager_start(uint32_t ttl_ms, uint32_t interval_ms) {
    static uint8_t stack[2048];

    memset(&g_sync_mgr, 0, sizeof(g_sync_mgr));
    g_sync_mgr.ttl_ms      = ttl_ms ? ttl_ms : SYNC_TTL_DEFAULT_MS;
    g_sync_mgr.interval_ms  = interval_ms ? interval_ms : SYNC_INTERVAL_DEFAULT_MS;

    g_sync_mgr.thread = rt_thread_create(
        "sync_mgr",
        sync_manager_thread_entry,
        RT_NULL,
        sizeof(stack),
        5,
        20
    );

    rt_thread_startup(g_sync_mgr.thread);
    return APP_EOK;
}

int sync_manager_get_purge_count(module_id_e id) {
    if (id <= MODULE_ID_UNKNOWN || id >= MODULE_ID_MAX) return APP_EINVAL;
    return g_sync_mgr.purge_count[id];
}
