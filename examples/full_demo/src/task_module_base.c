#include "task_module_base.h"
#include <rtthread.h>

void module_thread_entry(task_module_t *module) {
    if (module == RT_NULL || module->ops == RT_NULL) return;

    if (module->ops->init) {
        int ret = module->ops->init(module);
        if (ret != APP_EOK) {
            module->task_cfg.curr_state = TASK_STATE_ERROR;
            return;
        }
    }

    msg_t msg;
    while (module->task_cfg.curr_state == TASK_STATE_RUNNING) {
        rt_err_t ret = rt_mq_recv((rt_mq_t)module->msg_queue,
                                  &msg, sizeof(msg),
                                  rt_tick_from_millisecond(module->msg_recv_timeout_ms));

        if (ret == RT_EOK) {
            if (module->ops->dispatch) {
                module->ops->dispatch(module, &msg);
            }
        } else if (ret == -RT_ETIMEOUT) {
            /* 超时 = tick 周期触发 */
            if (module->ops->tick) {
                module->ops->tick(module);
            }
        } else {
            module->task_cfg.curr_state = TASK_STATE_ERROR;
            break;
        }
    }
}
