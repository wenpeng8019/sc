/* ============================================================
 * gpu.c —— env 公共层：surface 句柄池、缺省值、后端选择与分派
 * ============================================================
 * 后端选择（运行时，同 wsi/glfw）：
 *   desc.backend == DEFAULT → 按编入顺序取平台默认：
 *     macOS: Metal > GL > NULL；Linux/Windows: GL > NULL。
 *   显式指定后端但未编入/初始化失败 → sc_gpu_init 返回 0（不静默回退）。
 * ============================================================ */

#include "internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

gpu_state g_gpu;

/* ---- 日志 ------------------------------------------------- */

void gpu_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sc_gpu] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ---- 句柄池 ----------------------------------------------- */

static void poolInit(gpu_pool* p, int num) {
    p->size = num + 1;                       /* 槽 0 保留 */
    p->queue_top = 0;
    p->free_queue = (int*)calloc((size_t)num, sizeof(int));
    p->gen = (uint32_t*)calloc((size_t)p->size, sizeof(uint32_t));
    for (int i = num; i >= 1; i--)           /* 低索引先出 */
        p->free_queue[p->queue_top++] = i;
}

static void poolFree(gpu_pool* p) {
    free(p->free_queue); p->free_queue = NULL;
    free(p->gen);        p->gen = NULL;
    p->size = 0; p->queue_top = 0;
}

static uint32_t poolAlloc(gpu_pool* p) {
    if (p->queue_top <= 0) return 0;
    int index = p->free_queue[--p->queue_top];
    uint32_t gen = ++p->gen[index];
    if ((gen & 0xFFFF) == 0) gen = ++p->gen[index];   /* 代数跳过 0 */
    return ((gen & 0xFFFF) << 16) | (uint32_t)index;
}

static void poolRelease(gpu_pool* p, uint32_t id) {
    int index = gpu_slot_index(id);
    if (index <= 0 || index >= p->size) return;
    p->free_queue[p->queue_top++] = index;
}

gpu_surface_t* gpu_lookup_surface(uint32_t id) {
    if (!id) return NULL;
    int index = gpu_slot_index(id);
    if (index <= 0 || index >= g_gpu.surface_pool.size) return NULL;
    gpu_surface_t* s = &g_gpu.surfaces[index];
    if (s->id != id) return NULL;
    return s;
}

gpu_memimg_t* gpu_lookup_memimg(uint32_t id) {
    if (!id) return NULL;
    int index = gpu_slot_index(id);
    if (index <= 0 || index >= g_gpu.memimg_pool.size) return NULL;
    gpu_memimg_t* m = &g_gpu.memimgs[index];
    if (m->id != id) return NULL;
    return m;
}

/* ---- 缺省值 ----------------------------------------------- */

#define DEF(v, d) ((v) == 0 ? (d) : (v))

static void resolveSurfaceDesc(sc_gpu_surface_desc* d) {
    d->color_format  = DEF(d->color_format, SC_GPU_PIXELFORMAT_BGRA8);
    d->depth_format  = DEF(d->depth_format, SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
    d->sample_count  = DEF(d->sample_count, 1);
    d->swap_interval = DEF(d->swap_interval, 1);
    if (d->kind == SC_GPU_SURFACE_MEMORY) {
        d->image_count = DEF(d->image_count, 3);
        if (d->image_count > SC_GPU_MAX_MEMORY_IMAGES)
            d->image_count = SC_GPU_MAX_MEMORY_IMAGES;
    }
}

/* 像素格式 → DRM fourcc（LE 内存序） */
static uint32_t formatToFourcc(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_RGBA8:   return SC_GPU_FOURCC('A','B','2','4'); /* DRM_FORMAT_ABGR8888 */
        case SC_GPU_PIXELFORMAT_DEFAULT:
        case SC_GPU_PIXELFORMAT_BGRA8:   return SC_GPU_FOURCC('A','R','2','4'); /* DRM_FORMAT_ARGB8888 */
        case SC_GPU_PIXELFORMAT_RGB10A2: return SC_GPU_FOURCC('A','B','3','0');
        default:                         return 0;
    }
}

/* ---- 后端选择 --------------------------------------------- */

static const gpu_env_api* pickBackend(sc_gpu_backend want) {
    switch (want) {
        case SC_GPU_BACKEND_METAL:
#ifdef SC_GPU_METAL
            return gpu_env_metal();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_GL:
#ifdef SC_GPU_GL
            return gpu_env_gl();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_VULKAN:
#ifdef SC_GPU_VULKAN
            return gpu_env_vulkan();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_D3D11:
#ifdef SC_GPU_D3D11
            return gpu_env_d3d11();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_NULL:
            return gpu_env_null();
        case SC_GPU_BACKEND_DEFAULT:
        default:
            break;
    }
#ifdef SC_GPU_METAL
    return gpu_env_metal();
#elif defined(SC_GPU_GL)
    return gpu_env_gl();
#elif defined(SC_GPU_VULKAN)
    return gpu_env_vulkan();
#else
    return gpu_env_null();
#endif
}

/* ---- 生命周期 --------------------------------------------- */

int sc_gpu_init(const sc_gpu_desc* desc) {
    if (g_gpu.valid) { gpu_log("init: 已初始化"); return 0; }
    memset(&g_gpu, 0, sizeof(g_gpu));
    g_gpu.desc = *desc;
    resolveSurfaceDesc(&g_gpu.desc.surface);
    g_gpu.desc.surface_pool_size = DEF(g_gpu.desc.surface_pool_size, 8);
    g_gpu.desc.memimg_pool_size  = DEF(g_gpu.desc.memimg_pool_size, 64);

    const gpu_env_api* api = pickBackend(g_gpu.desc.backend);
    if (!api) { gpu_log("init: 请求的后端未编入本库"); return 0; }

    poolInit(&g_gpu.surface_pool, g_gpu.desc.surface_pool_size);
    g_gpu.surfaces = (gpu_surface_t*)calloc(
        (size_t)g_gpu.surface_pool.size, sizeof(gpu_surface_t));
    poolInit(&g_gpu.memimg_pool, g_gpu.desc.memimg_pool_size);
    g_gpu.memimgs = (gpu_memimg_t*)calloc(
        (size_t)g_gpu.memimg_pool.size, sizeof(gpu_memimg_t));

    if (!api->init(&g_gpu.desc)) {
        gpu_log("init: 后端 %s 初始化失败", api->name);
        sc_gpu_shutdown();
        return 0;
    }
    g_gpu.api = api;
    g_gpu.valid = true;

    /* 提供了原生窗口 → 创建默认 surface 并置为当前 */
    if (g_gpu.desc.surface.native_window) {
        sc_gpu_surface s = sc_gpu_make_surface(&g_gpu.desc.surface);
        if (!s) {
            gpu_log("init: 默认 surface 创建失败");
            sc_gpu_shutdown();
            return 0;
        }
        sc_gpu_make_current(s);
    }
    return 1;
}

void sc_gpu_shutdown(void) {
    if (g_gpu.api) {
        for (int i = 1; i < g_gpu.surface_pool.size; i++)
            if (g_gpu.surfaces[i].state == GPU_SLOT_VALID)
                sc_gpu_destroy_surface(g_gpu.surfaces[i].id);
        for (int i = 1; i < g_gpu.memimg_pool.size; i++)
            if (g_gpu.memimgs[i].state == GPU_SLOT_VALID &&
                g_gpu.api->memimg_free)
                g_gpu.api->memimg_free(&g_gpu.memimgs[i]);
        g_gpu.api->shutdown();
    }
    free(g_gpu.surfaces);
    free(g_gpu.memimgs);
    poolFree(&g_gpu.surface_pool);
    poolFree(&g_gpu.memimg_pool);
    memset(&g_gpu, 0, sizeof(g_gpu));
}

int sc_gpu_isvalid(void) { return g_gpu.valid ? 1 : 0; }

int sc_gpu_query_backend(void) {
    return g_gpu.api ? (int)g_gpu.api->kind : (int)SC_GPU_BACKEND_NULL;
}

void sc_gpu_resize(int width, int height) {
    if (!g_gpu.valid || !g_gpu.cur_surface) return;
    sc_gpu_surface_resize(g_gpu.cur_surface_id, width, height);
}

/* ---- surface ---------------------------------------------- */

sc_gpu_surface sc_gpu_make_surface(const sc_gpu_surface_desc* desc) {
    if (!g_gpu.valid || !desc) return 0;
    if (desc->kind == SC_GPU_SURFACE_WINDOW && !desc->native_window) {
        gpu_log("make_surface: WINDOW 需 native_window"); return 0;
    }
    if (desc->kind == SC_GPU_SURFACE_MEMORY &&
        (desc->width <= 0 || desc->height <= 0)) {
        gpu_log("make_surface: MEMORY 需 width/height"); return 0;
    }
    uint32_t id = poolAlloc(&g_gpu.surface_pool);
    if (!id) { gpu_log("make_surface: 池满"); return 0; }
    gpu_surface_t* surf = &g_gpu.surfaces[gpu_slot_index(id)];
    memset(surf, 0, sizeof(*surf));
    surf->id = id;
    surf->desc = *desc;
    resolveSurfaceDesc(&surf->desc);
    surf->ring_cur = -1;

    /* MEMORY：先分配 memimg 环，后端按槽建渲染目标 */
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        sc_gpu_memimg_desc md;
        memset(&md, 0, sizeof(md));
        md.width = surf->desc.width;
        md.height = surf->desc.height;
        md.format = surf->desc.color_format;
        md.memory = surf->desc.memory;
        md.render_target = 1;
        md.label = surf->desc.label;
        int i = 0;
        for (; i < surf->desc.image_count; i++) {
            surf->ring_imgs[i] = sc_gpu_memimg_alloc(&md);
            if (!surf->ring_imgs[i]) break;
        }
        if (i < surf->desc.image_count) {
            gpu_log("make_surface: memimg 环分配失败（%d/%d）", i, surf->desc.image_count);
            while (i-- > 0) sc_gpu_memimg_free(surf->ring_imgs[i]);
            surf->id = 0;
            poolRelease(&g_gpu.surface_pool, id);
            return 0;
        }
    }

    surf->state = g_gpu.api->surface_create(surf) ? GPU_SLOT_VALID
                                                    : GPU_SLOT_FAILED;
    if (surf->state == GPU_SLOT_FAILED &&
        surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        for (int i = 0; i < surf->desc.image_count; i++)
            sc_gpu_memimg_free(surf->ring_imgs[i]);
    }
    return id;
}

void sc_gpu_destroy_surface(sc_gpu_surface hnd) {
    if (!g_gpu.valid) return;
    gpu_surface_t* surf = gpu_lookup_surface(hnd);
    if (!surf) return;
    if (g_gpu.cur_surface == surf) {
        g_gpu.cur_surface = NULL;
        g_gpu.cur_surface_id = 0;
        g_gpu.api->surface_activate(NULL);
    }
    if (surf->state == GPU_SLOT_VALID) g_gpu.api->surface_destroy(surf);
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        for (int i = 0; i < surf->desc.image_count; i++)
            if (surf->ring_imgs[i]) sc_gpu_memimg_free(surf->ring_imgs[i]);
    }
    surf->id = 0;
    surf->state = GPU_SLOT_FREE;
    poolRelease(&g_gpu.surface_pool, hnd);
}

void sc_gpu_make_current(sc_gpu_surface hnd) {
    if (!g_gpu.valid) return;
    gpu_surface_t* surf = gpu_lookup_surface(hnd);
    if (!surf || surf->state != GPU_SLOT_VALID) {
        gpu_log("make_current: 无效 surface"); return;
    }
    g_gpu.cur_surface = surf;
    g_gpu.cur_surface_id = hnd;
    g_gpu.api->surface_activate(surf);
}

sc_gpu_surface sc_gpu_query_current_surface(void) {
    return g_gpu.cur_surface_id;
}

void sc_gpu_surface_resize(sc_gpu_surface hnd, int width, int height) {
    if (!g_gpu.valid || width <= 0 || height <= 0) return;
    gpu_surface_t* surf = gpu_lookup_surface(hnd);
    if (!surf || surf->state != GPU_SLOT_VALID) return;
    if (surf->desc.width == width && surf->desc.height == height) return;
    surf->desc.width = width;
    surf->desc.height = height;
    g_gpu.api->surface_resize(surf, width, height);
}

int sc_gpu_query_surface_info(sc_gpu_surface hnd, sc_gpu_surface_desc* out) {
    if (!g_gpu.valid || !out) return 0;
    gpu_surface_t* surf = hnd ? gpu_lookup_surface(hnd) : g_gpu.cur_surface;
    if (!surf || surf->state != GPU_SLOT_VALID) return 0;
    *out = surf->desc;
    return 1;
}

/* ---- 帧交付 ------------------------------------------------ */

void* sc_gpu_device(void) {
    return (g_gpu.valid && g_gpu.api->device) ? g_gpu.api->device() : NULL;
}

int sc_gpu_frame_acquire(sc_gpu_frame* out) {
    if (!g_gpu.valid || !out) return 0;
    gpu_surface_t* surf = g_gpu.cur_surface;
    if (!surf) {
        gpu_log("frame_acquire: 无当前 surface（先 make_current）");
        return 0;
    }
    /* MEMORY：公共层调度环槽（后端按 ring_cur 填句柄） */
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY && surf->ring_cur < 0) {
        int slot = surf->ring_acquire;
        int st = sc_get_acq(&surf->ring_state[slot]);
        if (st != GPU_RING_FREE) {
            gpu_log("frame_acquire: 环满（消费端未归还）");
            return 0;
        }
        surf->ring_state[slot] = GPU_RING_ACQUIRED;
        surf->ring_cur = slot;
        if (g_gpu.mem_acquired_count < 16)
            g_gpu.mem_acquired[g_gpu.mem_acquired_count++] = surf;
    }
    memset(out, 0, sizeof(*out));
    return g_gpu.api->frame_acquire(surf, out) ? 1 : 0;
}

void sc_gpu_frame_end(void) {
    if (!g_gpu.valid) return;
    g_gpu.api->frame_end();   /* 后端先收尾（swap/fence，ring_cur 仍有效） */
    /* MEMORY 环推进：本帧渲染完的槽 → RENDERED（消费线程可见） */
    for (int i = 0; i < g_gpu.mem_acquired_count; i++) {
        gpu_surface_t* surf = g_gpu.mem_acquired[i];
        if (surf->ring_cur >= 0) {
            sc_set_rel(&surf->ring_state[surf->ring_cur], GPU_RING_RENDERED);
            surf->ring_acquire = (surf->ring_acquire + 1) % surf->desc.image_count;
            surf->ring_cur = -1;
        }
    }
    g_gpu.mem_acquired_count = 0;
}

/* ---- memimg ------------------------------------------------ */

static void resolveMemimgDesc(sc_gpu_memimg_desc* d) {
    d->format = DEF(d->format, SC_GPU_PIXELFORMAT_BGRA8);
    if (!d->fourcc) d->fourcc = formatToFourcc(d->format);
}

sc_gpu_memimg sc_gpu_memimg_alloc(const sc_gpu_memimg_desc* desc) {
    if (!g_gpu.valid || !desc || desc->width <= 0 || desc->height <= 0) return 0;
    if (!g_gpu.api->memimg_alloc) {
        gpu_log("memimg_alloc: 后端 %s 不支持", g_gpu.api->name);
        return 0;
    }
    uint32_t id = poolAlloc(&g_gpu.memimg_pool);
    if (!id) { gpu_log("memimg_alloc: 池满"); return 0; }
    gpu_memimg_t* img = &g_gpu.memimgs[gpu_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->id = id;
    img->desc = *desc;
    resolveMemimgDesc(&img->desc);
    if (!img->desc.fourcc) {
        gpu_log("memimg_alloc: 格式不支持");
        img->id = 0;
        poolRelease(&g_gpu.memimg_pool, id);
        return 0;
    }
    img->state = g_gpu.api->memimg_alloc(img) ? GPU_SLOT_VALID
                                                : GPU_SLOT_FAILED;
    return id;
}

sc_gpu_memimg sc_gpu_memimg_import(const sc_gpu_memory_frame* src) {
    if (!g_gpu.valid || !src) return 0;
    if (!g_gpu.api->memimg_import) {
        gpu_log("memimg_import: 后端 %s 不支持", g_gpu.api->name);
        return 0;
    }
    uint32_t id = poolAlloc(&g_gpu.memimg_pool);
    if (!id) { gpu_log("memimg_import: 池满"); return 0; }
    gpu_memimg_t* img = &g_gpu.memimgs[gpu_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->id = id;
    img->imported = true;
    img->desc.width = src->width;
    img->desc.height = src->height;
    img->desc.fourcc = src->fourcc;
    img->state = g_gpu.api->memimg_import(img, src) ? GPU_SLOT_VALID
                                                      : GPU_SLOT_FAILED;
    return id;
}

int sc_gpu_memimg_export(sc_gpu_memimg hnd, sc_gpu_memory_frame* out, int with_fence) {
    if (!g_gpu.valid || !out) return 0;
    gpu_memimg_t* img = gpu_lookup_memimg(hnd);
    if (!img || img->state != GPU_SLOT_VALID || !g_gpu.api->memimg_export) return 0;
    memset(out, 0, sizeof(*out));
    out->sync_fd = -1;
    if (!g_gpu.api->memimg_export(img, out, with_fence != 0)) return 0;
    out->img = hnd;
    return 1;
}

void* sc_gpu_memimg_native(sc_gpu_memimg hnd) {
    if (!g_gpu.valid) return NULL;
    gpu_memimg_t* img = gpu_lookup_memimg(hnd);
    if (!img || img->state != GPU_SLOT_VALID || !g_gpu.api->memimg_native) return NULL;
    return g_gpu.api->memimg_native(img);
}

void* sc_gpu_memimg_map(sc_gpu_memimg hnd, int plane, uint32_t* out_stride) {
    if (!g_gpu.valid) return NULL;
    gpu_memimg_t* img = gpu_lookup_memimg(hnd);
    if (!img || img->state != GPU_SLOT_VALID || !g_gpu.api->memimg_map) return NULL;
    return g_gpu.api->memimg_map(img, plane, out_stride);
}

void sc_gpu_memimg_unmap(sc_gpu_memimg hnd, int plane) {
    if (!g_gpu.valid) return;
    gpu_memimg_t* img = gpu_lookup_memimg(hnd);
    if (!img || img->state != GPU_SLOT_VALID || !g_gpu.api->memimg_unmap) return;
    g_gpu.api->memimg_unmap(img, plane);
}

void sc_gpu_memimg_free(sc_gpu_memimg hnd) {
    if (!g_gpu.valid) return;
    gpu_memimg_t* img = gpu_lookup_memimg(hnd);
    if (!img) return;
    if (img->state == GPU_SLOT_VALID && g_gpu.api->memimg_free)
        g_gpu.api->memimg_free(img);
    img->id = 0;
    img->state = GPU_SLOT_FREE;
    poolRelease(&g_gpu.memimg_pool, hnd);
}

/* ---- MEMORY surface 消费端 ---------------------------------- */

int sc_gpu_memory_dequeue(sc_gpu_surface hnd, sc_gpu_memory_frame* out) {
    if (!g_gpu.valid || !out) return 0;
    gpu_surface_t* surf = gpu_lookup_surface(hnd);
    if (!surf || surf->state != GPU_SLOT_VALID ||
        surf->desc.kind != SC_GPU_SURFACE_MEMORY) return 0;
    int slot = surf->ring_dequeue;
    int st = sc_get_acq(&surf->ring_state[slot]);
    if (st != GPU_RING_RENDERED) return 0;   /* 无成帧 */
    memset(out, 0, sizeof(*out));
    out->sync_fd = -1;
    if (!g_gpu.api->surface_dequeue ||
        !g_gpu.api->surface_dequeue(surf, slot, out)) return 0;
    out->img = surf->ring_imgs[slot];
    out->slot = (uint32_t)slot;
    sc_set_rel(&surf->ring_state[slot], GPU_RING_DEQUEUED);
    surf->ring_dequeue = (surf->ring_dequeue + 1) % surf->desc.image_count;
    return 1;
}

void sc_gpu_memory_enqueue(sc_gpu_surface hnd, uint32_t slot) {
    if (!g_gpu.valid) return;
    gpu_surface_t* surf = gpu_lookup_surface(hnd);
    if (!surf || surf->desc.kind != SC_GPU_SURFACE_MEMORY ||
        slot >= (uint32_t)surf->desc.image_count) return;
    int st = sc_get_acq(&surf->ring_state[slot]);
    if (st != GPU_RING_DEQUEUED) {
        gpu_log("memory_enqueue: 槽 %u 非消费中状态", slot);
        return;
    }
    sc_set_rel(&surf->ring_state[slot], GPU_RING_FREE);
}
