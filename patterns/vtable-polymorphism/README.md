# VTable Polymorphism — 虚拟方法表多态

> 来源：[CoOp (swfeiyu)](https://github.com/swfeiyu/coop) · C++ 虚函数表底层原理

## 一句话

在 C 语言里手动构建 C++ 虚函数表——所有方法指针收进一个结构体，不同实现绑定不同的表。

## C++ 虚函数表原理

```
C++ 编译器在幕后做的事:
┌──────────────┐     ┌──────────────┐
│ 对象实例     │ →   │ 虚函数表      │
│ [vptr]       │     │ [0] method1  │→ 实现A::method1
│ attr1        │     │ [1] method2  │→ 实现A::method2
│ attr2        │     └──────────────┘
└──────────────┘
```

## C 语言手动实现

```c
/* ===== 方法表（= C++ 的 vtable）===== */
typedef int (*init_fn_t)(void *self, void *cfg);
typedef int (*run_fn_t)(void *self);
typedef int (*stop_fn_t)(void *self);

typedef struct {
    init_fn_t init;
    run_fn_t  run;
    stop_fn_t stop;
} module_ops_t;

/* ===== 基类 ===== */
typedef struct {
    const module_ops_t *ops;   /* vtable 指针 */
    int state;
    void *ctx;
} module_t;

/* ===== 子类 A：LED 实现 ===== */
module_ops_t led_ops = { .init=led_init, .run=led_run, .stop=led_stop };
typedef struct { module_t base; int pin; } led_t;

/* ===== 子类 B：Motor 实现 ===== */
module_ops_t motor_ops = { .init=motor_init, .run=motor_run, .stop=motor_stop };
typedef struct { module_t base; int speed; } motor_t;

/* ===== 调用 — 多态 ===== */
led_t   red;   red.base.ops   = &led_ops;
motor_t servo; servo.base.ops = &motor_ops;

module_t *devices[] = { &red.base, &servo.base };
for (int i = 0; i < 2; i++)
    devices[i]->ops->run(devices[i]);  /* 多态——不用知道是 LED 还是 Motor */
```

## 与本框架的对应

`task_module_ops_t` 就是 vtable——`init/dispatch/transition/tick` 四个函数指针，LED 和 RS485 绑不同的实现，框架层调 `module->ops->dispatch(module, msg)` 就是多态调用。

## 参考资料

- [CoOp GitHub](https://github.com/swfeiyu/coop)
- C++ ABI: Itanium C++ ABI vtable layout
