#include "task_led.h"
#include "task_module_base.h"
#include "task_msg_bus.h"
#include <string.h>

#define LED_TASK_STACK_SIZE 1024
#define LED_MSG_QUEUE_DEPTH 8
#define LED_SCAN_PERIOD_MS 50

typedef enum
{
    LED_FSM_INIT = 0,
    LED_FSM_NORMAL,
    LED_FSM_FAULT,
} led_fsm_state_e;

typedef struct
{
    led_fsm_state_e fsm_state;
    uint32_t scan_period_ms;
    int is_on;
    uint32_t last_toggle_ms;
    uint32_t blink_interval_ms;
} led_ctx_t;

typedef struct
{
    task_module_t base;
    led_ctx_t priv;
} led_module_t;

/* ===== 前向声明 ===== */
static int led_module_init(task_module_t *module);
static int led_fsm_dispatch(task_module_t *module, const msg_t *msg);
static int led_fsm_transition(task_module_t *module, uint32_t next_state);
static int led_state_normal_handler(task_module_t *module, const msg_t *msg);
static void led_tick(task_module_t *module);

/* ===== 静态实例 ===== */
static uint8_t g_led_mq_pool[LED_MSG_QUEUE_DEPTH * sizeof(msg_t)];
static struct rt_messagequeue g_led_mq_obj;

static const task_module_ops_t g_led_ops = {
    .init = led_module_init,
    .dispatch = led_fsm_dispatch,
    .transition = led_fsm_transition,
    .tick = led_tick,
};

static led_module_t g_led_module = {
    .base.task_cfg.id = MODULE_ID_LED,
    .base.task_cfg.name = "led",
    .base.task_cfg.task_entry = task_led_entry,
    .base.task_cfg.prio = TASK_PRIO_MID,
    .base.task_cfg.stack_size = LED_TASK_STACK_SIZE,
    .base.task_cfg.task_param = &g_led_module.base,
    .base.task_cfg.curr_state = TASK_STATE_UNINIT,
    .base.msg_queue_depth = LED_MSG_QUEUE_DEPTH,
    .base.msg_size = sizeof(msg_t),
    .base.ops = &g_led_ops,
    .base.ctx = &g_led_module.priv,
};

/* ===== dispatch：只路由 ===== */
static int led_fsm_dispatch(task_module_t *module, const msg_t *msg)
{
    if (msg->msg_id == MSG_ID_SYSTEM_FAULT)
        return module->ops->transition(module, LED_FSM_FAULT);
    if (msg->msg_id == MSG_ID_MODULE_RESET)
        return module->ops->transition(module, LED_FSM_NORMAL);

    led_ctx_t *ctx = (led_ctx_t *)module->ctx;
    switch (ctx->fsm_state)
    {
    case LED_FSM_NORMAL:
        return led_state_normal_handler(module, msg);
    default:
        return APP_EINVAL;
    }
}

/* ===== transition：只做 entry ===== */
static int led_fsm_transition(task_module_t *module, uint32_t next_state)
{
    led_ctx_t *ctx = (led_ctx_t *)module->ctx;
    ctx->fsm_state = (led_fsm_state_e)next_state;

    switch ((led_fsm_state_e)next_state)
    {
    case LED_FSM_NORMAL:
        ctx->is_on = 0;
        ctx->blink_interval_ms = 0;
        /* Drv_LED_off(); — 实际项目调用 BSP */
        rt_kprintf("[LED] → NORMAL\n");
        break;
    case LED_FSM_FAULT:
        ctx->blink_interval_ms = 500; /* 告警闪烁 */
        rt_kprintf("[LED] → FAULT\n");
        break;
    default:
        break;
    }
    return APP_EOK;
}

/* ===== state handler：只做本状态业务 ===== */
static int led_state_normal_handler(task_module_t *module, const msg_t *msg)
{
    led_ctx_t *ctx = (led_ctx_t *)module->ctx;

    switch (msg->msg_id)
    {
    case LED_MSG_RUN_ON:
        ctx->is_on = 1;
        ctx->blink_interval_ms = 0;
        /* Drv_LED_on(); */ rt_kprintf("[LED] ON\n");
        return APP_EOK;
    case LED_MSG_RUN_OFF:
        ctx->is_on = 0;
        ctx->blink_interval_ms = 0;
        /* Drv_LED_off(); */ rt_kprintf("[LED] OFF\n");
        return APP_EOK;
    case LED_MSG_TEST_BLINK:
        ctx->blink_interval_ms = 200;
        return APP_EOK;
    default:
        return APP_EINVAL;
    }
}

/* ===== tick：只做周期扫描 ===== */
static void led_tick(task_module_t *module)
{
    led_ctx_t *ctx = (led_ctx_t *)module->ctx;
    if (ctx->blink_interval_ms == 0)
        return;

    uint32_t now = rt_tick_get_millisecond();
    if ((uint32_t)(now - ctx->last_toggle_ms) >= ctx->blink_interval_ms)
    {
        ctx->last_toggle_ms = now;
        ctx->is_on = !ctx->is_on;
        rt_kprintf("[LED] %s\n", ctx->is_on ? "ON" : "OFF");
    }
}

/* ===== init ===== */
static int led_module_init(task_module_t *module)
{
    led_ctx_t *ctx = (led_ctx_t *)module->ctx;
    memset(ctx, 0, sizeof(*ctx));
    ctx->fsm_state = LED_FSM_INIT;
    ctx->scan_period_ms = LED_SCAN_PERIOD_MS;

    rt_err_t ret = rt_mq_init(&g_led_mq_obj, "led_mq", g_led_mq_pool,
                              sizeof(msg_t), sizeof(g_led_mq_pool), RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK)
    {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }

    module->msg_queue = &g_led_mq_obj;
    module->msg_queue_depth = LED_MSG_QUEUE_DEPTH;
    module->msg_size = sizeof(msg_t);
    module->msg_recv_timeout_ms = LED_SCAN_PERIOD_MS;

    if (msg_bus_register_module(module) != APP_EOK)
    {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }

    module->ops->transition(module, LED_FSM_NORMAL);
    module->task_cfg.curr_state = TASK_STATE_RUNNING;
    return APP_EOK;
}

void task_led_entry(void *param)
{
    module_thread_entry(&g_led_module.base);
}
