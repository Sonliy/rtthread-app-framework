#ifndef __TASK_LED_H__
#define __TASK_LED_H__

typedef enum {
    LED_MSG_NONE = 0,
    LED_MSG_TEST_BLINK,
    LED_MSG_RUN_ON,
    LED_MSG_RUN_OFF,
    LED_MSG_FAULT_ON,
} led_msg_id_e;

void task_led_entry(void *param);

#endif
