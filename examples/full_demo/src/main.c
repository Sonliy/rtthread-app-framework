/**
 * RT-Thread 应用框架完整示例
 *
 * 架构: task_module_t 基类 + msg_bus 消息总线 + FSM 状态机
 * 模块: LED (练手模块) + COM_PC (RS485 通信模块)
 *
 * 三层分离: dispatch(路由) → transition(entry动作) → state_handler(业务)
 */
#include <rtthread.h>
#include "task_module_base.h"
#include "task_msg_bus.h"
#include "task_led.h"
#include "task_com_pc.h"
#include "task_sync_manager.h"

/* ===== 线程栈（静态分配）===== */
static rt_uint8_t g_led_stack[1024];
static rt_uint8_t g_com_pc_stack[2048];

/* ===== 模块入口声明 ===== */
extern void task_led_entry(void *param);
extern void task_com_pc_entry(void *param);

/* ===== 框架初始化 ===== */
static int app_framework_init(void) {
    /* 1. 初始化消息总线 */
    msg_bus_init();

    /* 2. 创建 LED 线程 */
    rt_thread_t led_tid = rt_thread_create("led",
        task_led_entry, RT_NULL,
        sizeof(g_led_stack),
        TASK_PRIO_MID, 10);
    if (led_tid != RT_NULL) {
        rt_thread_startup(led_tid);
        rt_kprintf("[APP] LED thread started\r\n");
    }

<｜｜DSML｜｜parameter name="new_string" string="true">    /* 3. 创建 COM_PC 线程 */
    rt_thread_t com_tid = rt_thread_create("com_pc",
        task_com_pc_entry, RT_NULL,
        sizeof(g_com_pc_stack),
        TASK_PRIO_MID, 10);
    if (com_tid != RT_NULL) {
        rt_thread_startup(com_tid);
        rt_kprintf("[APP] COM_PC thread started\r\n");
    }

    /* 4. 启动 SyncManager 超时清淤 */
    sync_manager_start(SYNC_TTL_DEFAULT_MS, SYNC_INTERVAL_DEFAULT_MS);
    rt_kprintf("[APP] SyncManager started\r\n");

    return APP_EOK;
}

/* ===== 系统入口 ===== */
int main(void) {
    rt_kprintf("\r\n=== RT-Thread App Framework Demo ===\r\n");

    app_framework_init();

    /* 启动调度器 */
    rt_kprintf("[APP] Starting scheduler...\r\n");
    return 0;
}
