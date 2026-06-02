/** OPF (*p)->f(p) 模式最小示例 */
#include <stdio.h>

/* ========== 接口层（头文件）========== */
typedef int (*painter_draw_fn_t)(void *self, int x, int y, int color);
struct painter_i { painter_draw_fn_t draw; };

/* ========== 类型转换 inline（隐藏 (*p)->f(p) 语法）========== */
static inline int painter_draw(void *self, int x, int y, int color) {
    return (*(struct painter_i **)self)->draw(self, x, y, color);
}

/* ========== 实现者 A：RGB 屏幕 ========== */
struct rgb_screen {
    struct painter_i *interface; /* ← 首字段 */
    int color_mask;
};

int rgb_screen_draw(void *self, int x, int y, int color) {
    struct rgb_screen *me = (struct rgb_screen *)self;
    printf("[RGB] draw at (%d,%d) color=0x%x mask=0x%x\n",
           x, y, color, me->color_mask);
    return 0;
}

static struct painter_i rgb_interface = { .draw = rgb_screen_draw };
void rgb_screen_init(struct rgb_screen *s) { s->interface = &rgb_interface; }

/* ========== 实现者 B：单色屏幕 ========== */
struct mono_screen {
    struct painter_i *interface;
    unsigned char buf[1024];
};

int mono_screen_draw(void *self, int x, int y, int color) { (void)self;
    printf("[MONO] draw at (%d,%d) color=%d\n", x, y, color ? 1 : 0);
    return 0;
}

static struct painter_i mono_interface = { .draw = mono_screen_draw };
void mono_screen_init(struct mono_screen *s) { s->interface = &mono_interface; }

/* ========== 业务层（只依赖 painter_i，不依赖具体屏幕）========== */
void draw_ui(void *painter) {
    painter_draw(painter, 0,   0,  0xFF0000);
    painter_draw(painter, 100, 50, 0x00FF00);
}

/* ========== 使用 ========== */
int main(void) {
    struct rgb_screen  scr1; rgb_screen_init(&scr1);
    struct mono_screen scr2; mono_screen_init(&scr2);

    draw_ui(&scr1);  /* RGB 屏幕 */
    draw_ui(&scr2);  /* 单色屏幕 — 同一套业务代码，两个不同硬件 */

    return 0;
}
