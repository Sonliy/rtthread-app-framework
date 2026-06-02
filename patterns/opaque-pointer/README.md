# Opaque Pointer / Handle Pattern

> 来源：ESP-IDF 官方框架 · Espressif Developer Blog (2025.10)
> 参考：`httpd_handle_t`、`i2c_master_bus_handle_t` 等 ESP-IDF 标准 API

## 一句话

头文件只暴露 `typedef struct xxx_t xxx_t`（不透明类型），实际 struct 定义放在 .c 里——外部只能通过函数接口访问，无法直接读写内部成员。

## 模式

```c
/* ===== xxx.h（公开 API）===== */
typedef struct led_t led_t;            /* 不透明 */

led_t *led_create(int pin);
int    led_on(led_t *self);
int    led_off(led_t *self);
void   led_destroy(led_t *self);

/* ===== xxx.c（隐藏实现）===== */
struct led_t {                         /* 外部看不见 */
    int pin;
    int is_on;
};

led_t *led_create(int pin) {
    led_t *l = malloc(sizeof(led_t));  /* 或静态池 */
    l->pin = pin; l->is_on = 0;
    return l;
}
```

## 为什么不用直接暴露 struct

| 暴露 struct | 不透明指针 |
|------------|-----------|
| `led.pin = 5` — 随便改 | 只能通过 `led_on()` 调用 |
| 改内部字段 = 所有调用方炸 | 改内部实现，API 不变 |
| 编译时全暴露 | 使用者只看到 .h 的内容 |

## 与本框架的对应

本框架的 `task_module_t.ctx` 就是不透明指针——`led_ctx_t` 定义在 `task_led.c` 里，外部通过 `(led_ctx_t *)module->ctx` 强转访问。ESP-IDF 的 handle 模式把这一层推得更彻底——连结构体名字都 typedef 包装了。

## 参考资料

- [Espressif: OOP in C (ESP-IDF)](https://developer.espressif.com/blog/2025/10/oop_with_c/)
