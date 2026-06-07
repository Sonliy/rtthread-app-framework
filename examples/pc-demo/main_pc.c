/**
 * main_pc.c — task_module_t 框架 PC 端运行入口
 *
 * 编译: make
 * 运行: ./demo.exe
 */

#include "rtos_mock.h"
#include "task_module_base.h"
#include "task_msg_bus.h"
#include "task_led.h"
#include "task_com_pc.h"

/* 模块入口声明 */
extern void task_led_entry(void *param);
extern void task_com_pc_entry(void *param);

int main(void) {
    printf("\n=== task_module_t Framework PC Demo ===\n\n");

    /* 1. 初始化消息总线 */
    msg_bus_init();
    printf("[APP] msg_bus initialized\n");

    /* 2. 创建 LED 线程 */
    rt_thread_create("led", task_led_entry, RT_NULL, 1024, 5, 10);
    printf("[APP] LED thread started\n");

    /* 3. 创建 COM_PC 线程 */
    rt_thread_create("com_pc", task_com_pc_entry, RT_NULL, 2048, 5, 10);
    printf("[APP] COM_PC thread started\n");

    /* 4. 等待几秒让各模块输出日志 */
    rt_thread_mdelay(5000);

    printf("\n=== Demo Finished ===\n");
    return 0;
}
