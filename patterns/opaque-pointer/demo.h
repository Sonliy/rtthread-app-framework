/** 不透明指针模式 — 头文件（公开 API）*/
#ifndef LED_OPAQUE_H
#define LED_OPAQUE_H

typedef struct led_t led_t;   /* 不透明——外部只知道名字 */

int   led_init(led_t *self, int pin);
int   led_on(led_t *self);
int   led_off(led_t *self);

#endif
