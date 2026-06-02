/** 不透明指针模式 — 源文件（隐藏实现）+ 使用示例 */
#include <stdio.h>
#include "demo.h"

/* === 只有 .c 里能看到结构体 === */
struct led_t {
    int pin;
    int is_on;
};

int led_init(led_t *self, int pin)  { self->pin = pin; self->is_on = 0; return 0; }
int led_on(led_t *self)             { printf("[LED%d] ON\n", self->pin); self->is_on = 1; return 0; }
int led_off(led_t *self)            { printf("[LED%d] OFF\n", self->pin); self->is_on = 0; return 0; }

/* ========== 使用 ========== */
int main(void) {
    led_t red;                       /* 外部不能访问 red.pin — demo.h 里没暴露 struct 定义 */
    led_init(&red, 5);
    led_on(&red);
    led_off(&red);
    return 0;
}
