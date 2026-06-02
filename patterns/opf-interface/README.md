# OPF Interface — `(*p)->f(p)` Pattern

> 来源：[OPF Programming](https://opf-programming.github.io/) · B 站 "一点五编程"
> 别名：Object Pointer Forwarding · One Point Five Programming

## 一句话

接口指针必须放在结构体第一个字段，通过 `(*p)->f(p)` 实现 C 语言中的接口与多态。

## 核心机制

```c
// 接口定义（后缀 _i）
struct painter_i {
    int (*draw_point)(void *self, Point pos, int color);
};

// 实现者：interface 必须在第一个字段
struct rgb_screen {
    struct painter_i *interface;  // ← 首位
    int color_type;
};

// 类型转换 inline
static inline int draw_point(void *self, Point pos, int color) {
    return (*(struct painter_i **)self)->draw_point(self, pos, color);
}

// 调用
struct rgb_screen scr;
rgb_screen_init(&scr);
draw_point(&scr, (Point){10, 20}, RED);
// 等价于: (*(struct painter_i **)&scr)->draw_point(&scr, pos, color)
```

## 为什么首字段约束有效

```
内存布局:
┌─────────────────┐
│ interface 指针   │ ← self 指向这里，*(self) 直接拿到 interface
├─────────────────┤
│ color_type      │
└─────────────────┘

(*(struct painter_i **)self)->draw_point(self, ...)
       ↓
  self 强转为二级指针 → 解引用拿到 interface → 调用方法 → 把 self 传回去
```

## 对比本框架的 task_module_t

| 维度 | OPF | task_module_t |
|------|-----|---------------|
| 接口指针位置 | 必须首字段 | ops 在 task_cfg 之后 |
| 调用方式 | `(*p)->f(p)` | `module->ops->dispatch(module, msg)` |
| 多接口支持 | 链表+偏移 | 单接口 4 回调 vtable |
| 复杂度 | 极简学术风格 | 工程化可直接生产 |

## 参考资料

- [OPF 官方站](https://opf-programming.github.io/)
- [B 站 UP 主 "一点五编程"](https://space.bilibili.com/328353019)
