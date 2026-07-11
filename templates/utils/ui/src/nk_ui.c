#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* memfd_create */
#endif

#include "ui_internal.h"

#if defined(SC_UI_NK)

/* ============================================================
 * ui Nuklear 后端（Linux：X11 + Wayland）
 * ============================================================
 * 思路：ui.c 维护 retained 控件树；本后端把该树用 Nuklear 立即模式
 * 每帧重建并用 rawfb 软件光栅出图，再经 wsi 暴露的原生句柄呈现：
 * – X11：XPutImage 将 XImage blit 到窗口；
 * – Wayland：自建 wl_shm 缓冲区 attach/commit 到 wsi 的 wl_surface。
 * 输入来自 wsi 的窗口回调，喂给 nk_input。
 *
 * 现有 app 事件循环（while(!should_close) wsi_wait_events()）不需改动：
 * 本后端在 ui 内部注册 wsi 回调，事件到来即重绘并呈现。
 * ============================================================ */

#include <stdlib.h>
#include <string.h>

#include "../../wsi/wsi.h"

/* ---- Nuklear（单 TU 内含实现）---- */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_IMPLEMENTATION
#include "../vendor/nuklear.h"
#define NK_RAWFB_IMPLEMENTATION
#include "../vendor/nuklear_rawfb.h"

#if defined(WSI_X11) || P_LINUX || P_UNIX
 #define NKUI_X11 1
 #include <X11/Xlib.h>
 #include <X11/Xutil.h>
#endif

#if P_LINUX || P_UNIX
 #define NKUI_WAYLAND 1
 #include <wayland-client.h>
 #include <sys/mman.h>
 #include <unistd.h>
#endif

/* ============================================================
 * 每个 ctx 的后端状态（挂在 root window 的 backend 字段）
 * ============================================================ */

typedef struct nkui_state
{
    sc_ui_ctx*   ctx;
    sc_window*   win;      /* wsi 窗口 */
    int          platform;

    struct rawfb_context* rawfb;
    void*        fb;       /* 像素缓冲（w*h*4，物理像素） */
    void*        tex;      /* 字体图集内存（alpha8） */
    size_t       texSize;  /* tex 缓冲字节数（加载大字库时可扩容） */
    int          w;        /* 物理像素宽（= logW * scale） */
    int          h;        /* 物理像素高（= logH * scale） */
    int          logW;     /* 逻辑宽（窗口点） */
    int          logH;     /* 逻辑高（窗口点） */
    int          scale;    /* HiDPI 整数缩放：物理 = 逻辑 * scale（Wayland buffer_scale） */
    char         fontPath[512]; /* 已加载字体路径（scale 变化时重烘焙用） */
    float        fontSize;  /* 已加载字体逻辑字号 */
    struct nk_color clear;

    int          cursorX;  /* 最近鼠标位置（供 button 事件用） */
    int          cursorY;
    int          inputOpen; /* nk_input_begin 已调用 */

#if NKUI_X11
    Display*     dpy;
    Window       xwin;
    GC           gc;
    XImage*      ximg;
    Visual*      visual;
    int          depth;
#endif
#if NKUI_WAYLAND
    struct wl_display*  wl_display;
    struct wl_surface*  wl_surface;
    struct wl_registry* wl_registry;
    struct wl_shm*      wl_shm;
    struct wl_shm_pool* wl_pool;
    struct wl_buffer*   wl_buf[2];   /* 双缓冲：交替绘制，消除撕裂/闪烁 */
    void*               wl_fb[2];    /* 两块缓冲各自的 mmap 视图 */
    int                 wl_busy[2];  /* 该缓冲是否仍被合成器持有（未 release） */
    int                 wl_back;     /* 当前绘制的后备缓冲下标 */
    size_t              wl_poolSize; /* 整个 pool 的 mmap 字节数（两块） */
    void*               wl_poolBase; /* pool mmap 起始地址（供 munmap） */
    struct wl_callback* wl_frameCb;  /* frame 回调：驱动重绘循环 */
    struct { struct nkui_state* st; int idx; } wl_rel[2]; /* release 监听数据 */
#endif
} nkui_state;

/* ctx → state 的小型注册表：避免占用 wsi 的 window user_data。 */
#define NKUI_MAX 16
static sc_window* g_nkui_wins[NKUI_MAX];
static nkui_state* g_nkui_states[NKUI_MAX];

static void nkui_register(sc_window* win, nkui_state* st)
{
    int i;
    for (i = 0; i < NKUI_MAX; i++)
    {
        if (!g_nkui_wins[i])
        {
            g_nkui_wins[i] = win;
            g_nkui_states[i] = st;
            return;
        }
    }
}

static void nkui_unregister(sc_window* win)
{
    int i;
    for (i = 0; i < NKUI_MAX; i++)
    {
        if (g_nkui_wins[i] == win)
        {
            g_nkui_wins[i] = NULL;
            g_nkui_states[i] = NULL;
            return;
        }
    }
}

static nkui_state* nkui_lookup(sc_window* win)
{
    int i;
    for (i = 0; i < NKUI_MAX; i++)
        if (g_nkui_wins[i] == win)
            return g_nkui_states[i];
    return NULL;
}

/* ============================================================
 * 每控件后端状态（编辑框需要可变缓冲）
 * ============================================================ */

typedef struct nkui_ctrl
{
    char buf[256];
    int  len;
} nkui_ctrl;

static nkui_ctrl* nkui_ctrl_of(sc_ui_control* c)
{
    return (nkui_ctrl*) c->backend;
}

/* ============================================================
 * 像素格式（从 X11 visual 推导 rawfb_pl）
 * ============================================================ */

#if NKUI_X11
static int nkui_mask_shift(unsigned long mask)
{
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; s++; }
    return s;
}

static struct rawfb_pl nkui_pl_from_visual(Visual* v)
{
    struct rawfb_pl pl;
    memset(&pl, 0, sizeof(pl));
    pl.bytesPerPixel = 4;
    pl.rshift = (unsigned char) nkui_mask_shift(v->red_mask);
    pl.gshift = (unsigned char) nkui_mask_shift(v->green_mask);
    pl.bshift = (unsigned char) nkui_mask_shift(v->blue_mask);
    pl.ashift = 24; /* 未用 alpha */
    pl.rloss = 0;
    pl.gloss = 0;
    pl.bloss = 0;
    pl.aloss = 8; /* 无 alpha 通道 */
    return pl;
}
#endif

/* ============================================================
 * Wayland：wl_shm 缓冲区（自建，attach 到 wsi 的 wl_surface）
 * ============================================================ */

#if NKUI_WAYLAND
static void nkui_wl_reg_global(void* data, struct wl_registry* reg,
                               uint32_t name, const char* iface, uint32_t ver)
{
    nkui_state* st = (nkui_state*) data;
    (void) ver;
    if (strcmp(iface, "wl_shm") == 0)
        st->wl_shm = (struct wl_shm*) wl_registry_bind(reg, name, &wl_shm_interface, 1);
}

static void nkui_wl_reg_remove(void* data, struct wl_registry* reg, uint32_t name)
{
    (void) data; (void) reg; (void) name;
}

static const struct wl_registry_listener nkui_wl_reg_listener = {
    nkui_wl_reg_global,
    nkui_wl_reg_remove
};

static void nkui_wl_destroy_buffer(nkui_state* st)
{
    int i;
    if (st->wl_frameCb) { wl_callback_destroy(st->wl_frameCb); st->wl_frameCb = NULL; }
    for (i = 0; i < 2; i++)
    {
        if (st->wl_buf[i]) { wl_buffer_destroy(st->wl_buf[i]); st->wl_buf[i] = NULL; }
        st->wl_fb[i] = NULL;
        st->wl_busy[i] = 0;
    }
    if (st->wl_pool)   { wl_shm_pool_destroy(st->wl_pool);  st->wl_pool = NULL; }
    if (st->wl_poolBase && st->wl_poolSize) { munmap(st->wl_poolBase, st->wl_poolSize); }
    st->wl_poolBase = NULL;
    st->wl_poolSize = 0;
    st->wl_back = 0;
    st->fb = NULL;
}

/* wl_buffer.release：合成器用完该缓冲，标记为可复用。 */
static void nkui_wl_buf_release(void* data, struct wl_buffer* buf)
{
    struct { struct nkui_state* st; int idx; }* r = data;
    (void) buf;
    if (r && r->st && r->idx >= 0 && r->idx < 2)
        r->st->wl_busy[r->idx] = 0;
}
static const struct wl_buffer_listener nkui_wl_buf_listener = { nkui_wl_buf_release };

/* 创建双 XRGB8888 shm 缓冲（同一 pool 的两段）；成功后 wl_fb[0/1] 可写。 */
static int nkui_wl_create_buffer(nkui_state* st, int w, int h)
{
    int stride = w * 4;
    size_t bufSize = (size_t) stride * (size_t) h;
    size_t poolSize = bufSize * 2u;
    int fd, i;
    void* data;

    if (!st->wl_shm || w <= 0 || h <= 0)
        return 0;

    fd = memfd_create("sc-ui-nk", MFD_CLOEXEC);
    if (fd < 0)
        return 0;
    if (ftruncate(fd, (off_t) poolSize) < 0) { close(fd); return 0; }

    data = mmap(NULL, poolSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return 0; }

    st->wl_pool = wl_shm_create_pool(st->wl_shm, fd, (int32_t) poolSize);
    for (i = 0; i < 2; i++)
    {
        st->wl_buf[i] = wl_shm_pool_create_buffer(st->wl_pool, (int32_t) (i * bufSize),
                                                  w, h, stride, WL_SHM_FORMAT_XRGB8888);
        st->wl_fb[i] = (char*) data + i * bufSize;
        st->wl_busy[i] = 0;
        st->wl_rel[i].st = st;
        st->wl_rel[i].idx = i;
        wl_buffer_add_listener(st->wl_buf[i], &nkui_wl_buf_listener, &st->wl_rel[i]);
    }
    close(fd); /* pool 已 dup fd */

    st->wl_poolBase = data;
    st->wl_poolSize = poolSize;
    st->wl_back = 0;
    st->fb = st->wl_fb[0];
    st->w = w;
    st->h = h;
    return 1;
}

/* frame 回调：合成器可接受新帧时触发，驱动下一次绘制（立即模式重绘循环）。 */
static void nkui_frame(nkui_state* st); /* 前置声明 */
static void nkui_wl_frame_done(void* data, struct wl_callback* cb, uint32_t t)
{
    nkui_state* st = (nkui_state*) data;
    (void) t;
    if (st->wl_frameCb == cb) st->wl_frameCb = NULL;
    wl_callback_destroy(cb);
    nkui_frame(st); /* 会在 present 中重新登记 frame 回调，形成循环 */
}
static const struct wl_callback_listener nkui_wl_frame_listener = { nkui_wl_frame_done };

static void nkui_wl_present(nkui_state* st)
{
    int idx = st->wl_back;
    int s = st->scale > 0 ? st->scale : 1;
    if (!st->wl_surface || !st->wl_buf[idx])
        return;
    /* 以物理像素分配缓冲、buffer_scale 设为同一 scale：surface 逻辑尺寸
     * = 物理/scale = 窗口几何，既铺满又与装饰对齐。双缓冲交替消除撕裂。 */
    wl_surface_set_buffer_scale(st->wl_surface, s);
    wl_surface_attach(st->wl_surface, st->wl_buf[idx], 0, 0);
    wl_surface_damage(st->wl_surface, 0, 0, st->w / s, st->h / s);

    /* 登记下一帧回调，驱动重绘循环（wsi_wait_events 会因 frame-done 唤醒）。 */
    if (!st->wl_frameCb)
    {
        st->wl_frameCb = wl_surface_frame(st->wl_surface);
        if (st->wl_frameCb)
            wl_callback_add_listener(st->wl_frameCb, &nkui_wl_frame_listener, st);
    }

    wl_surface_commit(st->wl_surface);
    if (st->wl_display)
        wl_display_flush(st->wl_display);

    st->wl_busy[idx] = 1; /* 该缓冲已提交给合成器，release 前不可再画 */
}

/* 选一块未被合成器占用的后备缓冲；两块都在用则返回 -1（跳过本帧，避免撕裂）。 */
static int nkui_wl_pick_back(nkui_state* st)
{
    if (!st->wl_busy[st->wl_back])
        return st->wl_back;
    if (!st->wl_busy[1 - st->wl_back])
        return 1 - st->wl_back;
    return -1;
}
#endif /* NKUI_WAYLAND */

/* ============================================================
 * rawfb 初始化 / 尺寸调整
 * ============================================================ */

static void nkui_alloc_fb(nkui_state* st, int w, int h)
{
    size_t px = (size_t) w * (size_t) h * 4u;
    st->fb = realloc(st->fb, px);
    st->w = w;
    st->h = h;
}

/* 释放像素缓冲：Wayland 走 wl_shm 卸载，其余是 malloc。 */
static void nkui_free_fb(nkui_state* st)
{
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        nkui_wl_destroy_buffer(st);
        return;
    }
#endif
    free(st->fb);
    st->fb = NULL;
}

static int nkui_rawfb_setup(nkui_state* st)
{
    struct rawfb_pl pl;
    memset(&pl, 0, sizeof(pl));

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11 && st->visual)
        pl = nkui_pl_from_visual(st->visual);
    else
#endif
    {
        pl.bytesPerPixel = 4;
        pl.rshift = 16; pl.gshift = 8; pl.bshift = 0; pl.ashift = 24;
        pl.rloss = pl.gloss = pl.bloss = 0; pl.aloss = 8;
    }

    if (!st->tex)
    {
        st->tex = malloc(1024 * 1024); /* 字体 alpha8 图集：默认（ASCII）字体足够；加载系统字库时按需扩容 */
        st->texSize = 1024 * 1024;
    }
    if (!st->tex)
        return 0;

    st->rawfb = nk_rawfb_init(st->fb, st->tex, (unsigned) st->w, (unsigned) st->h,
                              (unsigned) (st->w * 4), pl);
    if (!st->rawfb)
        return 0;

    /* 去掉窗口内边距，使 layout_space 坐标 = 像素坐标 */
    st->rawfb->ctx.style.window.padding = nk_vec2(0, 0);
    st->rawfb->ctx.style.window.group_padding = nk_vec2(0, 0);
    st->rawfb->ctx.style.window.spacing = nk_vec2(0, 0);
    return 1;
}

/* ============================================================
 * 字体：可选加载系统 TTF（scc 不内置字库，尤其中文字库庞大）
 * ============================================================
 * 默认字体仅含 ASCII，中文/CJK 会显示为 ?/方块。应用可调用
 * sc_ui_set_font 指定一个系统字体路径；path 为 NULL 时在常见系统
 * 路径中自动探测一个含 CJK 字形的字体作为兜底。 */

static int nkui_file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* 常见 Linux 系统字体候选（优先含 CJK 者），仅在 path=NULL 时探测。 */
static int nkui_find_system_font(char* out, size_t cap)
{
    static const char* candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", /* 无 CJK，最后兜底 */
        NULL
    };
    int i;
    for (i = 0; candidates[i]; ++i)
    {
        if (nkui_file_exists(candidates[i]))
        {
            size_t n = strlen(candidates[i]);
            if (n + 1 > cap)
                continue;
            memcpy(out, candidates[i], n + 1);
            return 1;
        }
    }
    return 0;
}

/* 用自定义 TTF + CJK 字形范围重新烘焙 rawfb 图集并设为当前字体。
 * 成功返回 1；失败保持原字体（尽量回退到默认字体）并返回 0。 */
static int nkui_load_font(nkui_state* st, const char* path, float size)
{
    struct nk_font_atlas* atlas;
    struct nk_font_config cfg;
    struct nk_font* font;
    const void* image;
    int iw = 0, ih = 0;
    size_t need;
    float px;
    int scale;

    if (!st || !st->rawfb || !path)
        return 0;
    if (size <= 0.0f)
        size = 14.0f;

    /* 记住逻辑字号与路径，以便 scale 变化时重新烘焙。 */
    st->fontSize = size;
    if (path != st->fontPath)
    {
        strncpy(st->fontPath, path, sizeof(st->fontPath) - 1);
        st->fontPath[sizeof(st->fontPath) - 1] = '\0';
    }

    scale = st->scale > 0 ? st->scale : 1;
    px = size * (float) scale; /* 物理像素字号，保证 HiDPI 下清晰 */

    atlas = &st->rawfb->atlas;

    nk_font_atlas_clear(atlas);
    nk_font_atlas_init_default(atlas);
    nk_font_atlas_begin(atlas);

    cfg = nk_font_config(px);
    cfg.oversample_h = 1;
    cfg.oversample_v = 1;
    cfg.pixel_snap = nk_true;
    cfg.range = nk_font_chinese_glyph_ranges(); /* ASCII + 常用 CJK */

    font = nk_font_atlas_add_from_file(atlas, path, px, &cfg);
    if (!font)
    {
        /* 加载失败：烘焙默认字体，保持可用（bake 在无字体时自动补默认字体）。 */
        image = nk_font_atlas_bake(atlas, &iw, &ih, NK_FONT_ATLAS_ALPHA8);
        if (image && (size_t) iw * (size_t) ih <= st->texSize)
        {
            memcpy(st->tex, image, (size_t) iw * (size_t) ih);
            st->rawfb->font_tex.w = iw;
            st->rawfb->font_tex.h = ih;
            st->rawfb->font_tex.pitch = iw;
        }
        nk_font_atlas_end(atlas, nk_handle_ptr(NULL), NULL);
        if (atlas->default_font)
            nk_style_set_font(&st->rawfb->ctx, &atlas->default_font->handle);
        nk_style_load_all_cursors(&st->rawfb->ctx, atlas->cursors);
        return 0;
    }

    image = nk_font_atlas_bake(atlas, &iw, &ih, NK_FONT_ATLAS_ALPHA8);
    if (!image || iw <= 0 || ih <= 0)
        return 0;

    need = (size_t) iw * (size_t) ih; /* alpha8：1 字节/像素 */
    if (need > st->texSize)
    {
        void* nt = realloc(st->tex, need);
        if (!nt)
            return 0;
        st->tex = nt;
        st->texSize = need;
        st->rawfb->font_tex.pixels = st->tex;
    }
    memcpy(st->tex, image, need);
    st->rawfb->font_tex.w = iw;
    st->rawfb->font_tex.h = ih;
    st->rawfb->font_tex.pitch = iw;

    nk_font_atlas_end(atlas, nk_handle_ptr(NULL), NULL);
    nk_style_set_font(&st->rawfb->ctx, &font->handle);
    nk_style_load_all_cursors(&st->rawfb->ctx, atlas->cursors);
    return 1;
}

/* ============================================================
 * 帧构建：遍历控件树 → Nuklear 立即模式
 * ============================================================ */

static enum nk_keys nkui_map_key(int key)
{
    switch (key)
    {
        case SC_KEY_BACKSPACE: return NK_KEY_BACKSPACE;
        case SC_KEY_DELETE:    return NK_KEY_DEL;
        case SC_KEY_ENTER:     return NK_KEY_ENTER;
        case SC_KEY_TAB:       return NK_KEY_TAB;
        case SC_KEY_LEFT:      return NK_KEY_LEFT;
        case SC_KEY_RIGHT:     return NK_KEY_RIGHT;
        case SC_KEY_UP:        return NK_KEY_UP;
        case SC_KEY_DOWN:      return NK_KEY_DOWN;
        case SC_KEY_HOME:      return NK_KEY_TEXT_LINE_START;
        case SC_KEY_END:       return NK_KEY_TEXT_LINE_END;
        default:               return NK_KEY_NONE;
    }
}

static void nkui_build(nkui_state* st)
{
    struct nk_context* nk = &st->rawfb->ctx;
    sc_ui_control* c;
    int count = 0;
    float s = (float) (st->scale > 0 ? st->scale : 1);

    for (c = st->ctx->controlsHead; c; c = c->next)
        count++;

    if (nk_begin(nk, "sc_ui_root", nk_rect(0, 0, (float) st->w, (float) st->h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND))
    {
        nk_layout_space_begin(nk, NK_STATIC, (float) st->h, count);

        for (c = st->ctx->controlsHead; c; c = c->next)
        {
            const char* text = c->text ? c->text : "";
            nk_layout_space_push(nk, nk_rect((float) c->x * s, (float) c->y * s,
                                             (float) c->width * s, (float) c->height * s));
            switch (c->kind)
            {
                case SC_UI_LABEL:
                    nk_label(nk, text, NK_TEXT_LEFT);
                    break;

                case SC_UI_TEXT:
                    nk_label_wrap(nk, text);
                    break;

                case SC_UI_BUTTON:
                    if (nk_button_label(nk, text))
                        ui_emit_event(c, SC_UI_EVENT_CLICK);
                    break;

                case SC_UI_CHECKBOX:
                {
                    int on = c->checked ? nk_true : nk_false;
                    nk_checkbox_label(nk, text, &on);
                    if ((on ? 1 : 0) != c->checked)
                    {
                        c->checked = on ? 1 : 0;
                        ui_emit_event(c, SC_UI_EVENT_TOGGLE);
                    }
                    break;
                }

                case SC_UI_RADIOBOX:
                {
                    int newon = nk_option_label(nk, text, c->checked ? nk_true : nk_false);
                    if ((newon ? 1 : 0) != c->checked)
                    {
                        c->checked = newon ? 1 : 0;
                        ui_emit_event(c, SC_UI_EVENT_TOGGLE);
                    }
                    break;
                }

                case SC_UI_EDIT:
                {
                    nkui_ctrl* cc = nkui_ctrl_of(c);
                    if (cc)
                    {
                        nk_edit_string(nk, NK_EDIT_SIMPLE, cc->buf, &cc->len,
                                       (int) sizeof(cc->buf) - 1, nk_filter_default);
                        cc->buf[cc->len] = '\0';
                        if (!c->text || strcmp(c->text, cc->buf) != 0)
                        {
                            free(c->text);
                            c->text = strdup(cc->buf);
                            ui_emit_event(c, SC_UI_EVENT_TEXT);
                        }
                    }
                    break;
                }

                case SC_UI_COMBO:
                {
                    if (c->items && c->itemCount > 0)
                    {
                        int sel = c->selectedIndex < 0 ? 0 : c->selectedIndex;
                        int newsel = nk_combo(nk, (const char**) c->items, c->itemCount, sel,
                                              (int) (20 * s), nk_vec2((float) c->width * s, 200 * s));
                        if (newsel != c->selectedIndex)
                        {
                            c->selectedIndex = newsel;
                            ui_emit_event(c, SC_UI_EVENT_SELECT);
                        }
                    }
                    else
                    {
                        nk_label(nk, text, NK_TEXT_LEFT);
                    }
                    break;
                }

                case SC_UI_LIST:
                {
                    /* MVP：把列表项渲染为可选标签（无独立滚动组） */
                    int i;
                    for (i = 0; i < c->itemCount; i++)
                    {
                        int selected = (i == c->selectedIndex);
                        if (nk_select_label(nk, c->items[i], NK_TEXT_LEFT, selected)
                            && i != c->selectedIndex)
                        {
                            c->selectedIndex = i;
                            ui_emit_event(c, SC_UI_EVENT_SELECT);
                        }
                    }
                    break;
                }

                default:
                    nk_label(nk, text, NK_TEXT_LEFT);
                    break;
            }
        }

        nk_layout_space_end(nk);
    }
    nk_end(nk);
}

/* ============================================================
 * 呈现：把 rawfb 缓冲 blit 到窗口
 * ============================================================ */

static void nkui_present(nkui_state* st)
{
#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11 && st->dpy && st->ximg)
    {
        XPutImage(st->dpy, st->xwin, st->gc, st->ximg, 0, 0, 0, 0,
                  (unsigned) st->w, (unsigned) st->h);
        XFlush(st->dpy);
        return;
    }
#endif
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        nkui_wl_present(st);
        return;
    }
#endif
    (void) st;
}

/* 同步 HiDPI 缩放：Wayland 的 buffer_scale 可能在窗口首次进入输出后才由
 * 合成器改为 2（晚于 create），因此每帧检测；scale 变化时按物理像素重建缓冲、
 * 重设 rawfb、并按新 scale 重烘焙字体，保证铺满且清晰。 */
static void nkui_sync_scale(nkui_state* st)
{
#if NKUI_WAYLAND
    float sx = 1.f, sy = 1.f;
    int s;
    int pw, ph;
    struct rawfb_pl pl;

    if (st->platform != SC_PLATFORM_WAYLAND)
        return;
    if (st->win)
        sc_wsi_win_get_content_scale(st->win, &sx, &sy);
    s = (int) (sx + 0.5f);
    if (s < 1)
        s = 1;
    if (s == st->scale)
        return;

    st->scale = s;
    pw = st->logW * s;
    ph = st->logH * s;
    if (pw <= 0 || ph <= 0)
        return;


    nkui_wl_destroy_buffer(st);
    if (!nkui_wl_create_buffer(st, pw, ph)) /* 设置 st->w/st->h = 物理像素 */
        return;

    memset(&pl, 0, sizeof(pl));
    pl.bytesPerPixel = 4; pl.rshift = 16; pl.gshift = 8; pl.bshift = 0; pl.ashift = 24; pl.aloss = 8;
    if (st->rawfb)
        nk_rawfb_resize_fb(st->rawfb, st->fb, (unsigned) pw, (unsigned) ph,
                           (unsigned) (pw * 4), pl);

    /* 按新 scale 重烘焙已加载字体（物理字号 = 逻辑字号 * scale）。 */
    if (st->fontPath[0])
        nkui_load_font(st, st->fontPath, st->fontSize);
#else
    (void) st;
#endif
}

/* 一帧：结束输入批 → 构建 → 光栅 → 呈现 → 开新输入批 */
static void nkui_frame(nkui_state* st)
{
    if (!st || !st->rawfb)
        return;

    nkui_sync_scale(st);

#if NKUI_WAYLAND
    /* Wayland 双缓冲：选一块空闲后备缓冲绘制；两块都在合成器手里则跳过本帧，
     * 绝不往正被显示的缓冲里写 —— 这是消除鼠标移动时撕裂/闪烁的关键。 */
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        int back = nkui_wl_pick_back(st);
        if (back < 0)
            return; /* 无空闲缓冲，本帧丢弃；已挂起的 frame 回调会再次驱动 */
        st->wl_back = back;
        st->fb = st->wl_fb[back];
        st->rawfb->fb.pixels = st->fb;
    }
#endif

    if (st->inputOpen)
    {
        nk_input_end(&st->rawfb->ctx);
        st->inputOpen = 0;
    }

    nkui_build(st);
    nk_rawfb_render(st->rawfb, st->clear, 1);
    nkui_present(st);

    nk_input_begin(&st->rawfb->ctx);
    st->inputOpen = 1;
}

/* ============================================================
 * wsi 回调
 * ============================================================ */

static void on_refresh(sc_window* w)
{
    nkui_state* st = nkui_lookup(w);
    if (st) nkui_frame(st);
}

static void on_size(sc_window* w, int width, int height)
{
    nkui_state* st = nkui_lookup(w);
    struct rawfb_pl pl;
    int pw, ph, s;
    if (!st || width <= 0 || height <= 0)
        return;

    /* width/height：Wayland 为逻辑点、X11 为物理像素（scale 恒为 1）。 */
    st->logW = width;
    st->logH = height;
    s = st->scale > 0 ? st->scale : 1;
    pw = width * s;
    ph = height * s;

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11)
    {
        if (st->ximg)
        {
            st->ximg->data = NULL; /* 缓冲由我们持有，避免 XDestroyImage 释放 */
            XDestroyImage(st->ximg);
            st->ximg = NULL;
        }
        nkui_alloc_fb(st, pw, ph);
    }
#endif
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        nkui_wl_destroy_buffer(st);
        if (!nkui_wl_create_buffer(st, pw, ph))
            return;
    }
#endif

    memset(&pl, 0, sizeof(pl));
#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11 && st->visual)
        pl = nkui_pl_from_visual(st->visual);
    else
#endif
    { pl.bytesPerPixel = 4; pl.rshift = 16; pl.gshift = 8; pl.bshift = 0; pl.ashift = 24; pl.aloss = 8; }

    if (st->rawfb)
        nk_rawfb_resize_fb(st->rawfb, st->fb, (unsigned) pw, (unsigned) ph,
                           (unsigned) (pw * 4), pl);

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11 && st->dpy)
    {
        st->ximg = XCreateImage(st->dpy, st->visual, st->depth, ZPixmap, 0,
                                (char*) st->fb, (unsigned) pw, (unsigned) ph, 32,
                                pw * 4);
    }
#endif
    nkui_frame(st);
}

static void on_cursor_pos(sc_window* w, double x, double y)
{
    nkui_state* st = nkui_lookup(w);
    int s;
    if (!st) return;
    s = st->scale > 0 ? st->scale : 1;
    /* WSI 光标坐标为逻辑点；控件按物理像素布局，故输入也按 scale 放大。 */
    st->cursorX = (int) (x * s);
    st->cursorY = (int) (y * s);
    nk_input_motion(&st->rawfb->ctx, st->cursorX, st->cursorY);
    nkui_frame(st);
}

static void on_mouse_button(sc_window* w, int button, int action, int mods)
{
    nkui_state* st = nkui_lookup(w);
    enum nk_buttons b;
    int down;
    (void) mods;
    if (!st) return;

    if (button == SC_MOUSE_BUTTON_RIGHT)       b = NK_BUTTON_RIGHT;
    else if (button == SC_MOUSE_BUTTON_MIDDLE) b = NK_BUTTON_MIDDLE;
    else                                       b = NK_BUTTON_LEFT;

    down = (action != SC_RELEASE) ? nk_true : nk_false;
    nk_input_button(&st->rawfb->ctx, b, st->cursorX, st->cursorY, down);
    nkui_frame(st);
}

static void on_scroll(sc_window* w, double xoff, double yoff)
{
    nkui_state* st = nkui_lookup(w);
    if (!st) return;
    nk_input_scroll(&st->rawfb->ctx, nk_vec2((float) xoff, (float) yoff));
    nkui_frame(st);
}

static void on_key(sc_window* w, int key, int scancode, int action, int mods)
{
    nkui_state* st = nkui_lookup(w);
    enum nk_keys k;
    (void) scancode; (void) mods;
    if (!st) return;
    k = nkui_map_key(key);
    if (k != NK_KEY_NONE)
    {
        nk_input_key(&st->rawfb->ctx, k, action != SC_RELEASE ? nk_true : nk_false);
        nkui_frame(st);
    }
}

static void on_char(sc_window* w, unsigned int codepoint)
{
    nkui_state* st = nkui_lookup(w);
    if (!st) return;
    nk_input_unicode(&st->rawfb->ctx, (nk_rune) codepoint);
    nkui_frame(st);
}

static void nkui_install_callbacks(nkui_state* st)
{
    sc_wsi_win_cb cb;
    memset(&cb, 0, sizeof(cb));
    cb.refresh      = on_refresh;
    cb.size         = on_size;
    cb.cursor_pos   = on_cursor_pos;
    cb.mouse_button = on_mouse_button;
    cb.scroll       = on_scroll;
    cb.key          = on_key;
    cb.chr          = on_char;
    sc_wsi_win_set_callback(st->win, cb);
}

/* ============================================================
 * 后端 hook 实现
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win)
{
    /* 仅 root window（无 parent）承载 Nuklear 上下文 */
    if (!win || win->parent)
        return;

    nkui_state* st = (nkui_state*) calloc(1, sizeof(nkui_state));
    if (!st)
        return;

    st->ctx = win->ctx;
    st->win = win->ctx->window;
    st->platform = win->platform;
    st->clear = nk_rgba(48, 52, 58, 255);
    st->scale = 1; /* 初始按 1；Wayland 首帧 nkui_sync_scale 会按输出升到 2 等 */
    st->fontPath[0] = '\0';
    st->fontSize = 0.f;
    st->logW = win->width > 0 ? win->width : 1;
    st->logH = win->height > 0 ? win->height : 1;

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11)
    {
        XWindowAttributes attr;
        st->dpy = (Display*) win->nativeDisplay;
        st->xwin = (Window) (uintptr_t) win->nativeWindow;
        if (st->dpy && XGetWindowAttributes(st->dpy, st->xwin, &attr))
        {
            st->visual = attr.visual;
            st->depth = attr.depth;
            if (attr.width > 0)  st->logW = attr.width;
            if (attr.height > 0) st->logH = attr.height;
        }
        st->gc = st->dpy ? XCreateGC(st->dpy, st->xwin, 0, NULL) : NULL;
    }
#endif
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        st->wl_display = (struct wl_display*) win->nativeDisplay;
        st->wl_surface = (struct wl_surface*) win->nativeWindow;
        if (st->wl_display)
        {
            st->wl_registry = wl_display_get_registry(st->wl_display);
            wl_registry_add_listener(st->wl_registry, &nkui_wl_reg_listener, st);
            wl_display_roundtrip(st->wl_display); /* 取回 wl_shm */
        }
    }
#endif

    /* 物理像素尺寸（初始 scale=1，与逻辑相同）。 */
    st->w = st->logW * st->scale;
    st->h = st->logH * st->scale;

    /* 分配像素缓冲：Wayland 用 wl_shm mmap，其余用 malloc。 */
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
    {
        if (!nkui_wl_create_buffer(st, st->w, st->h))
        {
            free(st->tex);
            free(st);
            return;
        }
    }
    else
#endif
        nkui_alloc_fb(st, st->w, st->h);

    if (!nkui_rawfb_setup(st))
    {
        nkui_free_fb(st);
        free(st->tex);
        free(st);
        return;
    }

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11 && st->dpy)
    {
        st->ximg = XCreateImage(st->dpy, st->visual, st->depth, ZPixmap, 0,
                                (char*) st->fb, (unsigned) st->w, (unsigned) st->h, 32,
                                st->w * 4);
    }
#endif

    nk_input_begin(&st->rawfb->ctx);
    st->inputOpen = 1;

    win->backend = st;
    nkui_register(st->win, st);
    nkui_install_callbacks(st);

#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND)
        nkui_frame(st); /* 首帧 attach+commit，令 Wayland 映射并显示内容 */
#endif
}

void ui_backend_window_destroy(sc_ui_window* win)
{
    if (!win || win->parent || !win->backend)
        return;

    nkui_state* st = (nkui_state*) win->backend;
    nkui_unregister(st->win);

#if NKUI_X11
    if (st->platform == SC_PLATFORM_X11)
    {
        if (st->ximg)
        {
            st->ximg->data = NULL; /* fb 由我们释放 */
            XDestroyImage(st->ximg);
        }
        if (st->gc && st->dpy)
            XFreeGC(st->dpy, st->gc);
    }
#endif
    if (st->rawfb)
        nk_rawfb_shutdown(st->rawfb);
#if NKUI_WAYLAND
    if (st->platform == SC_PLATFORM_WAYLAND && st->wl_registry)
        wl_registry_destroy(st->wl_registry);
#endif
    nkui_free_fb(st);
    free(st->tex);
    free(st);
    win->backend = NULL;
}

void ui_backend_window_set_frame(sc_ui_window* win) { (void) win; }
void ui_backend_window_set_z(sc_ui_window* win) { (void) win; }

/* 控件模型变更后重绘一帧：把 retained 树的变化及时呈现。
 * （尤其 Wayland：空闲窗口不再收到事件，必须在模型变更时主动 commit。） */
static void nkui_refresh(sc_ui_control* control)
{
    if (control && control->ctx && control->ctx->rootWindow)
        nkui_frame((nkui_state*) control->ctx->rootWindow->backend);
}

void ui_backend_control_create(sc_ui_control* control)
{
    if (!control)
        return;
    /* 可编辑控件分配缓冲 */
    if (control->kind == SC_UI_EDIT || control->kind == SC_UI_TEXT)
    {
        nkui_ctrl* cc = (nkui_ctrl*) calloc(1, sizeof(nkui_ctrl));
        if (cc)
        {
            if (control->text)
            {
                strncpy(cc->buf, control->text, sizeof(cc->buf) - 1);
                cc->len = (int) strlen(cc->buf);
            }
            control->backend = cc;
        }
    }
    nkui_refresh(control);
}

void ui_backend_control_destroy(sc_ui_control* control)
{
    if (control && control->backend)
    {
        free(control->backend);
        control->backend = NULL;
    }
    nkui_refresh(control);
}

void ui_backend_control_set_frame(sc_ui_control* control) { nkui_refresh(control); }
void ui_backend_control_set_z(sc_ui_control* control) { nkui_refresh(control); }

void ui_backend_control_set_text(sc_ui_control* control)
{
    nkui_ctrl* cc;
    if (!control || !control->backend)
        return;
    cc = nkui_ctrl_of(control);
    if (control->text)
    {
        strncpy(cc->buf, control->text, sizeof(cc->buf) - 1);
        cc->buf[sizeof(cc->buf) - 1] = '\0';
    }
    else
    {
        cc->buf[0] = '\0';
    }
    cc->len = (int) strlen(cc->buf);
    nkui_refresh(control);
}

void ui_backend_control_set_checked(sc_ui_control* control) { nkui_refresh(control); }
void ui_backend_control_set_items(sc_ui_control* control) { nkui_refresh(control); }
void ui_backend_control_set_selected_index(sc_ui_control* control) { nkui_refresh(control); }

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    nkui_state* st;
    char resolved[512];
    if (!ctx || !ctx->rootWindow)
        return 0;
    st = (nkui_state*) ctx->rootWindow->backend;
    if (!st || !st->rawfb)
        return 0;
    if (!path)
    {
        if (!nkui_find_system_font(resolved, sizeof(resolved)))
            return 0;
        path = resolved;
    }
    if (!nkui_load_font(st, path, size))
        return 0;
    nkui_frame(st); /* 立即以新字体重绘一帧 */
    return 1;
}

#endif /* SC_UI_NK */
