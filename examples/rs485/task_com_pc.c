#include "task_com_pc.h"
#include "task_module_base.h"
#include "task_msg_bus.h"
#include "platconfig.h"
#include <string.h>

/* ========== 配置 ========== */
#define COM_PC_TASK_STACK_SIZE  2048
#define COM_PC_MSG_QUEUE_DEPTH  16
#define COM_PC_SCAN_PERIOD_MS   10
#define COM_PC_BUF_SIZE         1024

/* ========== UART 环形缓冲区（中断安全）========== */
static uint8_t  g_uart_rx_ring[COM_PC_BUF_SIZE];
static volatile uint16_t g_uart_rx_head = 0;
static volatile uint16_t g_uart_rx_tail = 0;

/* UART 接收中断回调（ISR 中只写环形缓冲区，不处理业务）*/
void UART0_RX_ISR(uint8_t byte) {
    uint16_t next = (g_uart_rx_head + 1) % COM_PC_BUF_SIZE;
    if (next != g_uart_rx_tail) {   /* 非满 */
        g_uart_rx_ring[g_uart_rx_head] = byte;
        g_uart_rx_head = next;
    }
    /* 满了就丢——生产环境应该上报溢出事件 */
}

/* ========== 上下文定义 ========== */
typedef enum {
    COM_PC_STATE_RECV = 0,   /* 接收态——等待数据 */
    COM_PC_STATE_SEND,        /* 发送态——发送响应 */
} com_pc_fsm_state_e;

typedef enum {
    COM_PC_MODE_NORMAL = 0,      /* 正常通信：收到什么回什么 */
    COM_PC_MODE_PASSTHROUGH,     /* 透传：收到的原始数据转发给上位机 */
} com_pc_mode_e;

typedef struct {
    com_pc_fsm_state_e fsm_state;
    com_pc_mode_e      mode;

    uint8_t rx_buf[COM_PC_BUF_SIZE];
    uint8_t tx_buf[COM_PC_BUF_SIZE];
    uint16_t rx_len;
    uint16_t tx_len;

    uint32_t scan_period_ms;
    uint32_t msg_recv_count;
    uint32_t idle_ticks;         /* 总线空闲计数 */
} com_pc_ctx_t;

typedef struct {
    task_module_t base;
    com_pc_ctx_t  priv;
} com_pc_module_t;

/* ========== 前向声明 ========== */
static int  com_pc_module_init(task_module_t *module);
static int  com_pc_fsm_dispatch(task_module_t *module, const msg_t *msg);
static int  com_pc_fsm_transition(task_module_t *module, uint32_t next_state);
static int  com_pc_state_recv_handler(task_module_t *module, const msg_t *msg);
static int  com_pc_state_send_handler(task_module_t *module, const msg_t *msg);
static void com_pc_tick(task_module_t *module);
static uint16_t uart_ring_available(void);
static uint16_t uart_ring_read(uint8_t *dst, uint16_t max_len);

/* ========== 静态实例 ========== */
static uint8_t g_st_com_pc_mq_pool[COM_PC_MSG_QUEUE_DEPTH * sizeof(msg_t)];
static struct rt_messagequeue g_st_com_pc_mq_obj;

static const task_module_ops_t g_com_pc_ops = {
    .init       = com_pc_module_init,
    .dispatch   = com_pc_fsm_dispatch,
    .transition = com_pc_fsm_transition,
    .tick       = com_pc_tick,
};

static com_pc_module_t g_st_com_pc_module = {
    .base.task_cfg.id         = MODULE_ID_COM_PC,
    .base.task_cfg.name       = "com_pc",
    .base.task_cfg.task_entry = task_com_pc_entry,
    .base.task_cfg.prio       = TASK_PRIO_MID,
    .base.task_cfg.stack_size = COM_PC_TASK_STACK_SIZE,
    .base.task_cfg.task_param = &g_st_com_pc_module.base,
    .base.task_cfg.curr_state = TASK_STATE_UNINIT,
    .base.msg_queue_depth     = COM_PC_MSG_QUEUE_DEPTH,
    .base.msg_size            = sizeof(msg_t),
    .base.ops                 = &g_com_pc_ops,
    .base.ctx                 = &g_st_com_pc_module.priv,
};

/* ========== dispatch：只做路由 ========== */
static int com_pc_fsm_dispatch(task_module_t *module, const msg_t *msg) {
    com_pc_ctx_t *ctx;

    if (module == RT_NULL || msg == RT_NULL) return APP_EINVAL;
    ctx = (com_pc_ctx_t *)module->ctx;
    if (ctx == RT_NULL) return APP_EINVAL;

    ctx->msg_recv_count++;

    /* 系统级消息 → transition */
    switch (msg->msg_id) {
    case MSG_ID_MODULE_RESET:
        return module->ops->transition(module, COM_PC_STATE_RECV);
    case MSG_ID_SYSTEM_FAULT:
        /* 故障态：可以在这切到 ERROR state，目前先不做 */
        return APP_EOK;
    default:
        break;
    }

    /* 业务消息 → 按当前 FSM 状态分发 */
    switch (ctx->fsm_state) {
    case COM_PC_STATE_RECV: return com_pc_state_recv_handler(module, msg);
    case COM_PC_STATE_SEND: return com_pc_state_send_handler(module, msg);
    default:                return APP_EINVAL;
    }
}

/* ========== transition：只做 entry 动作 ========== */
static int com_pc_fsm_transition(task_module_t *module, uint32_t next_state) {
    com_pc_ctx_t *ctx = (com_pc_ctx_t *)module->ctx;
    if (ctx == RT_NULL) return APP_EINVAL;

    ctx->fsm_state = (com_pc_fsm_state_e)next_state;

    switch ((com_pc_fsm_state_e)next_state) {
    case COM_PC_STATE_RECV:
        memset(ctx->rx_buf, 0, sizeof(ctx->rx_buf));
        ctx->rx_len = 0;
        ctx->idle_ticks = 0;
        rt_kprintf("[COM_PC] → RECV state\r\n");
        break;

    case COM_PC_STATE_SEND:
        rt_kprintf("[COM_PC] → SEND state\r\n");
        break;

    default: break;
    }
    return APP_EOK;
}

/* ========== 状态 handler：只做本状态下的业务 ========== */

/* RECV 状态：处理模式切换和测试命令 */
static int com_pc_state_recv_handler(task_module_t *module, const msg_t *msg) {
    com_pc_ctx_t *ctx = (com_pc_ctx_t *)module->ctx;

    switch (msg->msg_id) {

    case COM_PC_MSG_MODE_NORMAL:
        ctx->mode = COM_PC_MODE_NORMAL;
        rt_kprintf("[COM_PC] MODE → NORMAL\r\n");
        return APP_EOK;

    case COM_PC_MSG_MODE_PASSTHROUGH:
        ctx->mode = COM_PC_MODE_PASSTHROUGH;
        rt_kprintf("[COM_PC] MODE → PASSTHROUGH\r\n");
        return APP_EOK;

    case COM_PC_MSG_TEST_LOOPBACK:
        rt_kprintf("[COM_PC] TEST → loopback test\r\n");
        return APP_EOK;

    default:
        return APP_EINVAL;
    }
}

/* SEND 状态：处理发送完成后的回调 */
static int com_pc_state_send_handler(task_module_t *module, const msg_t *msg) {
    (void)module; (void)msg;
    /* SEND 状态下暂不处理额外消息，发送完成后 tick 会自动切回 RECV */
    return APP_EINVAL;
}

/* ========== tick：周期扫描（UART 收发）========== */
static void com_pc_tick(task_module_t *module) {
    com_pc_ctx_t *ctx = (com_pc_ctx_t *)module->ctx;
    if (ctx == RT_NULL) return;

    switch (ctx->fsm_state) {

    case COM_PC_STATE_RECV: {
        /* 从环形缓冲区读数据（非阻塞）*/
        uint16_t avail = uart_ring_available();
        if (avail == 0) {
            ctx->idle_ticks++;
            break;
        }
        ctx->idle_ticks = 0;

        if (ctx->mode == COM_PC_MODE_NORMAL) {
            ctx->rx_len = uart_ring_read(ctx->rx_buf, COM_PC_BUF_SIZE);
            if (ctx->rx_len > 0) {
                /* 收到数据 → 准备回发 → 切到 SEND 状态 */
                memcpy(ctx->tx_buf, ctx->rx_buf, ctx->rx_len);
                ctx->tx_len = ctx->rx_len;
                module->ops->transition(module, COM_PC_STATE_SEND);
            }
        } else if (ctx->mode == COM_PC_MODE_PASSTHROUGH) {
            /* 透传模式：读到数据直接转发到上位机（TODO）*/
            /* ctx->rx_len = uart_ring_read(ctx->rx_buf, COM_PC_BUF_SIZE);
               upstream_send(ctx->rx_buf, ctx->rx_len); */
        }
        break;
    }

    case COM_PC_STATE_SEND: {
        if (ctx->mode == COM_PC_MODE_NORMAL) {
            /* 发送数据 */
            platUartWrite(UART_PORT_0, ctx->tx_buf, ctx->tx_len);
            rt_kprintf("[COM_PC] TX %d bytes\r\n", ctx->tx_len);
        }
        /* 发完切回 RECV */
        module->ops->transition(module, COM_PC_STATE_RECV);
        break;
    }

    default: break;
    }
}

/* ========== init：创建队列 + 注册 + 初始化硬件 ========== */
static int com_pc_module_init(task_module_t *module) {
    com_pc_ctx_t *ctx;
    rt_err_t ret;

    if (module == RT_NULL) return APP_EINVAL;
    ctx = (com_pc_ctx_t *)module->ctx;
    if (ctx == RT_NULL) return APP_EINVAL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->fsm_state     = COM_PC_STATE_RECV;
    ctx->mode          = COM_PC_MODE_NORMAL;
    ctx->scan_period_ms = COM_PC_SCAN_PERIOD_MS;

    /* 创建消息队列 */
    ret = rt_mq_init(&g_st_com_pc_mq_obj,
                     "com_pc_mq",
                     g_st_com_pc_mq_pool,
                     sizeof(msg_t),
                     sizeof(g_st_com_pc_mq_pool),
                     RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK) {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }

    module->msg_queue          = &g_st_com_pc_mq_obj;
    module->msg_queue_depth    = COM_PC_MSG_QUEUE_DEPTH;
    module->msg_size           = sizeof(msg_t);
    module->msg_recv_timeout_ms = COM_PC_SCAN_PERIOD_MS;  /* 关键：非零才能触发 tick */

    /* 注册到消息总线 */
    if (msg_bus_register_module(module) != APP_EOK) {
        module->task_cfg.curr_state = TASK_STATE_ERROR;
        return APP_ERROR;
    }

    /* 初始化 UART 硬件 */
    platUartInit(UART_PORT_0, UART_BAUD_115200,
                 UART_DATA_BITS_8, UART_STOP_BITS_1, UART_PARITY_NONE);

    /* 进入 RECV 状态 */
    module->ops->transition(module, COM_PC_STATE_RECV);
    module->task_cfg.curr_state = TASK_STATE_RUNNING;
    return APP_EOK;
}

/* ========== 环形缓冲区工具函数 ========== */
static uint16_t uart_ring_available(void) {
    if (g_uart_rx_head >= g_uart_rx_tail)
        return g_uart_rx_head - g_uart_rx_tail;
    else
        return COM_PC_BUF_SIZE - g_uart_rx_tail + g_uart_rx_head;
}

static uint16_t uart_ring_read(uint8_t *dst, uint16_t max_len) {
    uint16_t cnt = 0;
    while (cnt < max_len && g_uart_rx_tail != g_uart_rx_head) {
        dst[cnt++] = g_uart_rx_ring[g_uart_rx_tail];
        g_uart_rx_tail = (g_uart_rx_tail + 1) % COM_PC_BUF_SIZE;
    }
    return cnt;
}

/* ========== 模块主入口 ========== */
void task_com_pc_entry(void *param) {
    module_thread_entry(&g_st_com_pc_module.base);
}
