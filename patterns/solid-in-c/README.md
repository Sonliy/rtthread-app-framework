# SOLID in C — 五原则落地示例

> 来源：GoF SOLID 原则 · 本框架 `task_module_t` 生产代码

## S — 单一职责（Single Responsibility）

**原则：** 一个函数/模块只做一件事，只因为一个原因被修改。

**本框架实现：**

```c
dispatch()    → 只做路由（系统消息→transition / 业务消息→state_handler）
transition()  → 只做 entry 动作（初始化新状态的硬件）
state_handler → 只做本状态下的业务动作
tick()        → 只做周期扫描
```

---

## O — 开闭原则（Open-Closed）

**原则：** 对扩展开放，对修改关闭。

**本框架实现：** `task_module_ops_t` vtable

```c
// 加新模块 → 实现 4 个回调，绑定 ops，框架代码不动
static const task_module_ops_t g_rs485_ops = {
    .init = rs485_init, .dispatch = rs485_dispatch,
    .transition = rs485_transition, .tick = rs485_tick,
};
// module_thread_entry 一行代码不用改
```

---

## L — 里氏替换（Liskov Substitution）

**原则：** 子类型可以无差别替换父类型。

**本框架实现：** 通用的模块线程入口

```c
void module_thread_entry(task_module_t *module) {
    // LED / RS485 / Flash / OTA 全部走同一个入口
    module->ops->init(module);
    while (module->task_cfg.curr_state == TASK_STATE_RUNNING) {
        rt_mq_recv(..., timeout);
        module->ops->dispatch(module, &msg);
    }
}
// 任何模块——不管它是 LED 还是 4G 通信——塞进去就能跑
```

---

## I — 接口隔离（Interface Segregation）

**原则：** 不用逼模块实现它不用的方法。

**本框架实现：** 可选回调

```c
// tick 是可选的——框架层做前向检查
if (module->ops->tick) {
    module->ops->tick(module);
}
// 不需要 tick 的模块填 NULL 就行
```

---

## D — 依赖倒置（Dependency Inversion）

**原则：** 高层不依赖低层，两者都依赖抽象。

**本框架实现：** msg_bus 只依赖 `task_module_t` 抽象

```c
int msg_bus_send(module_id_e src, module_id_e dest, uint32_t msg_id, ...) {
    task_module_t *target = g_msg_bus.module_table[dest];
    // 不知道目标是 LED 还是 RS485，只知道它是 task_module_t
    rt_mq_send_wait(target->msg_queue, &msg, sizeof(msg), timeout);
}
// 发送方不需要知道接收方的内部结构、状态机、硬件配置
```

---


## 参考资料

- [本框架源码](../../src/)
- GoF Design Patterns
- Linux 内核设计哲学
