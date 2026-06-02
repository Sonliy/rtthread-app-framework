#include "task_com_dsp.h"
#include "task_module_base.h"
#include "task_msg_bus.h"
#include <string.h>

/* ── PC 端 BSP 桩，引用 task_com_pc.c 中的实际实现 ── */
#define UART_PORT_DSP     1
#define UART_PORT_RS485   2
#define UART_BAUD_115200  115200
#define UART_DATA_BITS_8  8
#define UART_STOP_BITS_1   1
#define UART_PARITY_NONE   0

extern void platUartInit(int port, int baud, int bits, int stop, int parity);
extern int  platUartWrite(int port, const uint8_t *data, int len);

#define COM_DSP_TASK_STACK_SIZE 2048
#define COM_DSP_MSG_QUEUE_DEPTH 16
#define COM_DSP_SCAN_PERIOD_MS 10
#define COM_DSP_BUF_SIZE 1024
#define COM_DSP_POLL_INTERVAL_MS 200
#define COM_DSP_RECV_TIMEOUT_MS 50

/* ========== 状态枚举 ========== */
typedef enum
{
    COM_DSP_MODE_NORMAL = 0,
    COM_DSP_MODE_PASSTHROUGH,
    COM_DSP_MODE_UPDATE,
} com_dsp_mode_e;

typedef enum
{
    COM_DSP_STATE_SEND = 0, /* 发送轮询帧 */
    COM_DSP_STATE_RECV,     /* 等待 DSP 回复 */
} com_dsp_fsm_state_e;

typedef enum
{
    COM_DSP_UPG_IDLE = 0,
    COM_DSP_UPG_DOWNLOADING,
    COM_DSP_UPG_VERIFYING,
    COM_DSP_UPG_APPLYING,
} com_dsp_upg_state_e;

/* ========== 上下文 ========== */
typedef struct
{
    com_dsp_mode_e mode;
    com_dsp_fsm_state_e fsm_state;
    com_dsp_upg_state_e upg_state;
    uint32_t scan_period_ms;
    uint32_t last_poll_ms;
    uint32_t recv_start_ms; /* RECV 开始时间，用于超时检测 */
    uint32_t msg_recv_count;

    uint8_t poll_buf[COM_DSP_BUF_SIZE];
    uint16_t poll_len;

    uint8_t upg_buf[COM_DSP_BUF_SIZE];
    uint16_t upg_len;
    uint32_t upg_total_len;
    uint32_t upg_crc;
} com_dsp_ctx_t;

typedef struct
{
    task_module_t base;
    com_dsp_ctx_t priv;
} com_dsp_module_t;

/* ========== 前向声明 ========== */
static int com_dsp_module_init(task_module_t *module);
static int com_dsp_fsm_dispatch(task_module_t *module, const msg_t *msg);
static int com_dsp_fsm_transition(task_module_t *module, uint32_t next_state);
static void com_dsp_tick(task_module_t *module);
static void com_dsp_tick_normal(com_dsp_ctx_t *ctx, task_module_t *module);
static void com_dsp_tick_passthrough(com_dsp_ctx_t *ctx);
static void com_dsp_tick_update(com_dsp_ctx_t *ctx);

extern void UART_DSP_RX_ISR(uint8_t byte);
extern void UART_RS485_RX_ISR(uint8_t byte);
extern uint16_t uart_ring_rs485_read(uint8_t *dst, uint16_t max);
extern uint16_t uart_ring_dsp_read(uint8_t *dst, uint16_t max);

/* ========== 静态实例 ========== */
static uint8_t g_dsp_mq_pool[COM_DSP_MSG_QUEUE_DEPTH * sizeof(msg_t)];
static struct rt_messagequeue g_dsp_mq_obj;

static const task_module_ops_t g_dsp_ops = {
    .init = com_dsp_module_init,
    .dispatch = com_dsp_fsm_dispatch,
    .transition = com_dsp_fsm_transition,
    .tick = com_dsp_tick,
};

static com_dsp_module_t g_dsp_module = {
    .base.task_cfg.id = MODULE_ID_COM_PC,
    .base.task_cfg.name = "com_dsp",
    .base.task_cfg.task_entry = task_com_dsp_entry,
    .base.task_cfg.prio = TASK_PRIO_MID,
    .base.task_cfg.stack_size = COM_DSP_TASK_STACK_SIZE,
    .base.task_cfg.task_param = &g_dsp_module.base,
    .base.task_cfg.curr_state = TASK_STATE_UNINIT,
    .base.msg_queue_depth = COM_DSP_MSG_QUEUE_DEPTH,
    .base.msg_size = sizeof(msg_t),
    .base.ops = &g_dsp_ops,
    .base.ctx = &g_dsp_module.priv,
};

/* ========== dispatch ========== */
static int com_dsp_fsm_dispatch(task_module_t *module, const msg_t *msg)
{
    com_dsp_ctx_t *ctx = (com_dsp_ctx_t *)module->ctx;
    if (module == RT_NULL || msg == RT_NULL || ctx == RT_NULL)
        return APP_EINVAL;
    ctx->msg_recv_count++;

    /* 系统级消息 */
    if (msg->msg_id == MSG_ID_MODULE_RESET)
        return module->ops->transition(module, COM_DSP_MODE_NORMAL);

    /* UPDATE 模式下只接受升级消息 */
    if (ctx->mode == COM_DSP_MODE_UPDATE)
    {
        switch (msg->msg_id)
        {
        case COM_DSP_MSG_UPGRADE_FRAME:
            if (msg->data_len > 0 && ctx->upg_len + msg->data_len < COM_DSP_BUF_SIZE)
            {
                memcpy(ctx->upg_buf + ctx->upg_len, msg->data_buf, msg->data_len);
                ctx->upg_len += msg->data_len;
            }
            return APP_EOK;
        case COM_DSP_MSG_UPGRADE_DONE:
            return module->ops->transition(module, COM_DSP_MODE_NORMAL);
        default:
            return APP_EINVAL;
        }
    }

    /* NORMAL / PASSTHROUGH 下的模式切换 */
    switch (msg->msg_id)
    {
    case COM_DSP_MSG_MODE_PASSTHROUGH:
        return module->ops->transition(module, COM_DSP_MODE_PASSTHROUGH);
    case COM_DSP_MSG_MODE_UPDATE:
        return module->ops->transition(module, COM_DSP_MODE_UPDATE);
    default:
        return APP_EINVAL;
    }
}

/* ========== transition：切模式 + NORMAL 内切收发状态 ========== */
static int com_dsp_fsm_transition(task_module_t *module, uint32_t next_state)
{
    com_dsp_ctx_t *ctx = (com_dsp_ctx_t *)module->ctx;
    if (ctx == RT_NULL)
        return APP_EINVAL;

    /* ── 切模式：next_state 是 mode ── */
    if (next_state <= COM_DSP_MODE_UPDATE)
    {
        ctx->mode = (com_dsp_mode_e)next_state;
        switch ((com_dsp_mode_e)next_state)
        {
        case COM_DSP_MODE_NORMAL:
            ctx->fsm_state = COM_DSP_STATE_SEND;
            ctx->last_poll_ms = rt_tick_get_millisecond();
            /* 进入 NORMAL 后第一件事：发轮询帧 */
            break;
        case COM_DSP_MODE_PASSTHROUGH:
            break;
        case COM_DSP_MODE_UPDATE:
            ctx->upg_state = COM_DSP_UPG_IDLE;
            ctx->upg_len = ctx->upg_total_len = ctx->upg_crc = 0;
            break;
        }
        return APP_EOK;
    }

    /* ── 切收发状态：next_state 是 fsm_state（带 entry 动作）── */
    ctx->fsm_state = (com_dsp_fsm_state_e)next_state;
    switch ((com_dsp_fsm_state_e)next_state)
    {
    case COM_DSP_STATE_SEND:
        memset(ctx->poll_buf, 0, COM_DSP_BUF_SIZE);
        /* platUartWrite(UART_PORT_DSP, modbus_poll_frame, len); — 发轮询帧 */
        ctx->last_poll_ms = rt_tick_get_millisecond();
        break;
    case COM_DSP_STATE_RECV:
        memset(ctx->poll_buf, 0, COM_DSP_BUF_SIZE);
        ctx->poll_len = 0;
        ctx->recv_start_ms = rt_tick_get_millisecond();
        break;
    }
    return APP_EOK;
}

/* ========== tick ========== */
static void com_dsp_tick(task_module_t *module)
{
    com_dsp_ctx_t *ctx = (com_dsp_ctx_t *)module->ctx;
    if (ctx == RT_NULL)
        return;

    switch (ctx->mode)
    {
    case COM_DSP_MODE_NORMAL:
        com_dsp_tick_normal(ctx, module);
        break;
    case COM_DSP_MODE_PASSTHROUGH:
        com_dsp_tick_passthrough(ctx);
        break;
    case COM_DSP_MODE_UPDATE:
        com_dsp_tick_update(ctx);
        break;
    }
}

/* ── NORMAL：RECV / SEND FSM ── */
static void com_dsp_tick_normal(com_dsp_ctx_t *ctx, task_module_t *module)
{
    switch (ctx->fsm_state)
    {

    case COM_DSP_STATE_SEND:
    {
        /* 发完轮询帧 → 切 RECV 等回复 */
        module->ops->transition(module, COM_DSP_STATE_RECV);
        break;
    }

    case COM_DSP_STATE_RECV:
    {
        uint16_t n = uart_ring_dsp_read(ctx->poll_buf + ctx->poll_len,
                                        COM_DSP_BUF_SIZE - ctx->poll_len);
        ctx->poll_len += n;

        uint32_t now = rt_tick_get_millisecond();

        /* 收到完整帧 → 校验 → 回复给 COM_PC → 切 SEND 发下一帧 */
        if (ctx->poll_len >= 4)
        { /* TODO: 帧完整性检查 */
            /* msg_bus_send(src=DSP, dest=COM_PC, ... ); — 转发给上位机 */
            module->ops->transition(module, COM_DSP_STATE_SEND);
            break;
        }

        /* 超时 → 放弃这帧 → 发下一轮询 */
        if ((uint32_t)(now - ctx->recv_start_ms) >= COM_DSP_RECV_TIMEOUT_MS)
        {
            ctx->poll_len = 0;
            module->ops->transition(module, COM_DSP_STATE_SEND);
        }
        break;
    }
    }
}

/* ── PASSTHROUGH ── */
static void com_dsp_tick_passthrough(com_dsp_ctx_t *ctx)
{
    uint16_t n;
    (void)ctx;
    n = uart_ring_dsp_read(ctx->poll_buf, COM_DSP_BUF_SIZE);
    if (n > 0)
        platUartWrite(UART_PORT_RS485, ctx->poll_buf, n);
    n = uart_ring_rs485_read(ctx->poll_buf, COM_DSP_BUF_SIZE);
    if (n > 0)
        platUartWrite(UART_PORT_DSP, ctx->poll_buf, n);
}

/* ── UPDATE ── */
static void com_dsp_tick_update(com_dsp_ctx_t *ctx)
{
    switch (ctx->upg_state)
    {
    case COM_DSP_UPG_IDLE:
        if (ctx->upg_len > 0)
            ctx->upg_state = COM_DSP_UPG_DOWNLOADING;
        break;
    case COM_DSP_UPG_DOWNLOADING:
        platUartWrite(UART_PORT_DSP, ctx->upg_buf, ctx->upg_len);
        ctx->upg_len = 0;
        ctx->upg_state = COM_DSP_UPG_VERIFYING;
        break;
    case COM_DSP_UPG_VERIFYING:
        ctx->upg_state = COM_DSP_UPG_APPLYING;
        break;
    case COM_DSP_UPG_APPLYING:
        ctx->upg_state = COM_DSP_UPG_IDLE;
        break;
    }
}

/* ========== init ========== */
static int com_dsp_module_init(task_module_t *module)
{
    com_dsp_ctx_t *ctx = (com_dsp_ctx_t *)module->ctx;
    if (ctx == RT_NULL)
        return APP_EINVAL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->scan_period_ms = COM_DSP_SCAN_PERIOD_MS;

    rt_err_t ret = rt_mq_init(&g_dsp_mq_obj, "dsp_mq", g_dsp_mq_pool,
                              sizeof(msg_t), sizeof(g_dsp_mq_pool), RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK)
    {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }
    module->msg_queue = &g_dsp_mq_obj;
    module->msg_queue_depth = COM_DSP_MSG_QUEUE_DEPTH;
    module->msg_size = sizeof(msg_t);
    module->msg_recv_timeout_ms = COM_DSP_SCAN_PERIOD_MS;

    if (msg_bus_register_module(module) != APP_EOK)
    {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }

    platUartInit(UART_PORT_DSP, UART_BAUD_115200,
                 UART_DATA_BITS_8, UART_STOP_BITS_1, UART_PARITY_NONE);

    module->ops->transition(module, COM_DSP_MODE_NORMAL);
    module->task_cfg.curr_state = TASK_STATE_RUNNING;
    return APP_EOK;
}

void task_com_dsp_entry(void *param) { module_thread_entry(&g_dsp_module.base); }
