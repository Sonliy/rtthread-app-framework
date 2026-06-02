# rtthread-app-framework

基于 RT-Thread 的嵌入式应用框架，采用 **task_module_t** 基类 + **msg_bus** 消息总线 + **FSM** 状态机，实现 dispatch → transition → state_handler 三层分离。

## 设计原则

- **S 单一职责**：dispatch 只路由，transition 只做 entry 动作，state_handler 只做业务，tick 只做周期扫描
- **O 开闭原则**：加新模块 = 实现 4 个回调，框架代码不动
- **L 里氏替换**：`module_thread_entry()` 不依赖具体模块类型
- **I 接口隔离**：tick 可选，不需要的模块填 NULL
- **D 依赖倒置**：msg_bus 只依赖 `task_module_t` 抽象，不依赖具体模块

## 目录结构

```
├── examples/
│   ├── full_demo/       # 完整示例（RT-Thread 目标平台）
│   │   └── src/         # 14 个框架源文件
│   └── pc-demo/         # PC 端可编译运行版本
│       ├── rtos_mock.h   # RT-Thread API → pthread 抽象层
│       └── *.c/*.h       # 框架代码（核心逻辑不变）
└── patterns/            # OOP/架构设计模式参考
```

## PC 端快速体验

```bash
cd examples/pc-demo
make
./demo
```

无需 RT-Thread 环境，gcc + pthread 即可编译运行。所有硬件操作抽象为 printf，RT-Thread API 由 pthread 模拟。

## 模块架构

```
┌──────────────┐    msg_bus     ┌──────────────┐
│   task_led   │◄──────────────►│  task_com_pc  │
│   LED 模块   │                │  RS485 通信   │
└──────┬───────┘                └──────┬───────┘
       │                               │
       ▼                               ▼
┌──────────────────────────────────────────┐
│           task_module_t 基类              │
│  ops: init / dispatch / transition / tick │
│  ctx: private context (FSM state)         │
└──────────────────────────────────────────┘
```

## License

MIT
