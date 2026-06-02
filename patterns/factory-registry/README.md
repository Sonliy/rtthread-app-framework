# Factory Registry — 注册表工厂模式

> 来源：本框架 `msg_bus_register_module` + `module_id_e` 枚举的扩展

## 一句话

用编译期注册表替代 `if/else` 创建逻辑——加新模块只加一行注册项。

## 模式

```c
/* ===== 注册表（编译期确定，零运行时开销）===== */
static task_module_t *g_registry[MODULE_ID_MAX] = {
    [MODULE_ID_LED]     = &g_led_module.base,
    [MODULE_ID_UART]    = &g_rs485_module.base,
    [MODULE_ID_STORAGE] = &g_flash_module.base,
};

/* ===== 工厂函数 ===== */
task_module_t *module_factory_get(module_id_e id) {
    if (id < MODULE_ID_MAX && g_registry[id])
        return g_registry[id];
    return RT_NULL;
}

/* ===== 统一启动 ===== */
void app_framework_start(void) {
    for (int i = 1; i < MODULE_ID_MAX; i++) {
        task_module_t *m = module_factory_get((module_id_e)i);
        if (m) module_start(m);
    }
}
```

## 为什么不用 switch/if-else

| 手写 switch | 注册表 |
|------------|--------|
| 加新模块：多写一个 case | 加新模块：加一行数组项 |
| 容易漏掉新模块的创建 | 编译器帮你确认 |
| 启动代码越堆越长 | 启动代码永远不变 |

## 与本框架的对应

`msg_bus_register_module()` 已经是注册表模式——`module_table[module->id] = module`。工厂模式是往前推一步：把"创建"和"注册"合成一个入口。

## 参考资料

- GoF 工厂方法模式
- Linux 内核 `initcall` 机制（`module_init` 宏）
