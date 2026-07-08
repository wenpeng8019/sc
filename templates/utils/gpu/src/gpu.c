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

_sc_gpu_state _sc_gpu;

/* ---- 日志 ------------------------------------------------- */

void _sc_gpu_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sc_gpu] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ---- 句柄池 ----------------------------------------------- */

static void poolInit(_sc_gpu_pool* p, int num) {
    p->size = num + 1;                       /* 槽 0 保留 */
    p->queue_top = 0;
    p->free_queue = (int*)calloc((size_t)num, sizeof(int));
    p->gen = (uint32_t*)calloc((size_t)p->size, sizeof(uint32_t));
    for (int i = num; i >= 1; i--)           /* 低索引先出 */
        p->free_queue[p->queue_top++] = i;
}

static void poolFree(_sc_gpu_pool* p) {
    free(p->free_queue); p->free_queue = NULL;
    free(p->gen);        p->gen = NULL;
    p->size = 0; p->queue_top = 0;
}

static uint32_t poolAlloc(_sc_gpu_pool* p) {
    if (p->queue_top <= 0) return 0;
    int index = p->free_queue[--p->queue_top];
    uint32_t gen = ++p->gen[index];
    if ((gen & 0xFFFF) == 0) gen = ++p->gen[index];   /* 代数跳过 0 */
    return ((gen & 0xFFFF) << 16) | (uint32_t)index;
}

static void poolRelease(_sc_gpu_pool* p, uint32_t id) {
    int index = _sc_gpu_slot_index(id);
    if (index <= 0 || index >= p->size) return;
    p->free_queue[p->queue_top++] = index;
}

_sc_gpu_surface_t* _sc_gpu_lookup_surface(uint32_t id) {
    if (!id) return NULL;
    int index = _sc_gpu_slot_index(id);
    if (index <= 0 || index >= _sc_gpu.surface_pool.size) return NULL;
    _sc_gpu_surface_t* s = &_sc_gpu.surfaces[index];
    if (s->id != id) return NULL;
    return s;
}

_sc_gpu_memimg_t* _sc_gpu_lookup_memimg(uint32_t id) {
    if (!id) return NULL;
    int index = _sc_gpu_slot_index(id);
    if (index <= 0 || index >= _sc_gpu.memimg_pool.size) return NULL;
    _sc_gpu_memimg_t* m = &_sc_gpu.memimgs[index];
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

static const _sc_gpu_env_api* pickBackend(sc_gpu_backend want) {
    switch (want) {
        case SC_GPU_BACKEND_METAL:
#ifdef SC_GPU_METAL
            return _sc_gpu_env_metal();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_GL:
#ifdef SC_GPU_GL
            return _sc_gpu_env_gl();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_NULL:
            return _sc_gpu_env_null();
        case SC_GPU_BACKEND_DEFAULT:
        default:
            break;
    }
#ifdef SC_GPU_METAL
    return _sc_gpu_env_metal();
#elif defined(SC_GPU_GL)
    return _sc_gpu_env_gl();
#else
    return _sc_gpu_env_null();
#endif
}

/* ---- 生命周期 --------------------------------------------- */

int sc_gpu_init(const sc_gpu_desc* desc) {
    if (_sc_gpu.valid) { _sc_gpu_log("init: 已初始化"); return 0; }
    memset(&_sc_gpu, 0, sizeof(_sc_gpu));
    _sc_gpu.desc = *desc;
    resolveSurfaceDesc(&_sc_gpu.desc.surface);
    _sc_gpu.desc.surface_pool_size = DEF(_sc_gpu.desc.surface_pool_size, 8);
    _sc_gpu.desc.memimg_pool_size  = DEF(_sc_gpu.desc.memimg_pool_size, 64);

    const _sc_gpu_env_api* api = pickBackend(_sc_gpu.desc.backend);
    if (!api) { _sc_gpu_log("init: 请求的后端未编入本库"); return 0; }

    poolInit(&_sc_gpu.surface_pool, _sc_gpu.desc.surface_pool_size);
    _sc_gpu.surfaces = (_sc_gpu_surface_t*)calloc(
        (size_t)_sc_gpu.surface_pool.size, sizeof(_sc_gpu_surface_t));
    poolInit(&_sc_gpu.memimg_pool, _sc_gpu.desc.memimg_pool_size);
    _sc_gpu.memimgs = (_sc_gpu_memimg_t*)calloc(
        (size_t)_sc_gpu.memimg_pool.size, sizeof(_sc_gpu_memimg_t));

    if (!api->init(&_sc_gpu.desc)) {
        _sc_gpu_log("init: 后端 %s 初始化失败", api->name);
        sc_gpu_shutdown();
        return 0;
    }
    _sc_gpu.api = api;
    _sc_gpu.valid = true;

    /* 提供了原生窗口 → 创建默认 surface 并置为当前 */
    if (_sc_gpu.desc.surface.native_window) {
        sc_gpu_surface s = sc_gpu_make_surface(&_sc_gpu.desc.surface);
        if (!s) {
            _sc_gpu_log("init: 默认 surface 创建失败");
            sc_gpu_shutdown();
            return 0;
        }
        sc_gpu_make_current(s);
    }
    return 1;
}

void sc_gpu_shutdown(void) {
    if (_sc_gpu.api) {
        for (int i = 1; i < _sc_gpu.surface_pool.size; i++)
            if (_sc_gpu.surfaces[i].state == _SC_GPU_SLOT_VALID)
                sc_gpu_destroy_surface(_sc_gpu.surfaces[i].id);
        for (int i = 1; i < _sc_gpu.memimg_pool.size; i++)
            if (_sc_gpu.memimgs[i].state == _SC_GPU_SLOT_VALID &&
                _sc_gpu.api->memimg_free)
                _sc_gpu.api->memimg_free(&_sc_gpu.memimgs[i]);
        _sc_gpu.api->shutdown();
    }
    free(_sc_gpu.surfaces);
    free(_sc_gpu.memimgs);
    poolFree(&_sc_gpu.surface_pool);
    poolFree(&_sc_gpu.memimg_pool);
    memset(&_sc_gpu, 0, sizeof(_sc_gpu));
}

int sc_gpu_isvalid(void) { return _sc_gpu.valid ? 1 : 0; }

int sc_gpu_query_backend(void) {
    return _sc_gpu.api ? (int)_sc_gpu.api->kind : (int)SC_GPU_BACKEND_NULL;
}

void sc_gpu_resize(int width, int height) {
    if (!_sc_gpu.valid || !_sc_gpu.cur_surface) return;
    sc_gpu_surface_resize(_sc_gpu.cur_surface_id, width, height);
}

/* ---- surface ---------------------------------------------- */

sc_gpu_surface sc_gpu_make_surface(const sc_gpu_surface_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    if (desc->kind == SC_GPU_SURFACE_WINDOW && !desc->native_window) {
        _sc_gpu_log("make_surface: WINDOW 需 native_window"); return 0;
    }
    if (desc->kind == SC_GPU_SURFACE_MEMORY &&
        (desc->width <= 0 || desc->height <= 0)) {
        _sc_gpu_log("make_surface: MEMORY 需 width/height"); return 0;
    }
    uint32_t id = poolAlloc(&_sc_gpu.surface_pool);
    if (!id) { _sc_gpu_log("make_surface: 池满"); return 0; }
    _sc_gpu_surface_t* surf = &_sc_gpu.surfaces[_sc_gpu_slot_index(id)];
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
            _sc_gpu_log("make_surface: memimg 环分配失败（%d/%d）", i, surf->desc.image_count);
            while (i-- > 0) sc_gpu_memimg_free(surf->ring_imgs[i]);
            surf->id = 0;
            poolRelease(&_sc_gpu.surface_pool, id);
            return 0;
        }
    }

    surf->state = _sc_gpu.api->surface_create(surf) ? _SC_GPU_SLOT_VALID
                                                    : _SC_GPU_SLOT_FAILED;
    if (surf->state == _SC_GPU_SLOT_FAILED &&
        surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        for (int i = 0; i < surf->desc.image_count; i++)
            sc_gpu_memimg_free(surf->ring_imgs[i]);
    }
    return id;
}

void sc_gpu_destroy_surface(sc_gpu_surface hnd) {
    if (!_sc_gpu.valid) return;
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf) return;
    if (_sc_gpu.cur_surface == surf) {
        _sc_gpu.cur_surface = NULL;
        _sc_gpu.cur_surface_id = 0;
        _sc_gpu.api->surface_activate(NULL);
    }
    if (surf->state == _SC_GPU_SLOT_VALID) _sc_gpu.api->surface_destroy(surf);
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        for (int i = 0; i < surf->desc.image_count; i++)
            if (surf->ring_imgs[i]) sc_gpu_memimg_free(surf->ring_imgs[i]);
    }
    surf->id = 0;
    surf->state = _SC_GPU_SLOT_FREE;
    poolRelease(&_sc_gpu.surface_pool, hnd);
}

void sc_gpu_make_current(sc_gpu_surface hnd) {
    if (!_sc_gpu.valid) return;
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf || surf->state != _SC_GPU_SLOT_VALID) {
        _sc_gpu_log("make_current: 无效 surface"); return;
    }
    _sc_gpu.cur_surface = surf;
    _sc_gpu.cur_surface_id = hnd;
    _sc_gpu.api->surface_activate(surf);
}

sc_gpu_surface sc_gpu_query_current_surface(void) {
    return _sc_gpu.cur_surface_id;
}

void sc_gpu_surface_resize(sc_gpu_surface hnd, int width, int height) {
    if (!_sc_gpu.valid || width <= 0 || height <= 0) return;
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf || surf->state != _SC_GPU_SLOT_VALID) return;
    if (surf->desc.width == width && surf->desc.height == height) return;
    surf->desc.width = width;
    surf->desc.height = height;
    _sc_gpu.api->surface_resize(surf, width, height);
}

int sc_gpu_query_surface_info(sc_gpu_surface hnd, sc_gpu_surface_desc* out) {
    if (!_sc_gpu.valid || !out) return 0;
    _sc_gpu_surface_t* surf = hnd ? _sc_gpu_lookup_surface(hnd) : _sc_gpu.cur_surface;
    if (!surf || surf->state != _SC_GPU_SLOT_VALID) return 0;
    *out = surf->desc;
    return 1;
}

/* ---- 帧交付 ------------------------------------------------ */

void* sc_gpu_device(void) {
    return (_sc_gpu.valid && _sc_gpu.api->device) ? _sc_gpu.api->device() : NULL;
}

int sc_gpu_frame_acquire(sc_gpu_frame* out) {
    if (!_sc_gpu.valid || !out) return 0;
    _sc_gpu_surface_t* surf = _sc_gpu.cur_surface;
    if (!surf) {
        _sc_gpu_log("frame_acquire: 无当前 surface（先 make_current）");
        return 0;
    }
    /* MEMORY：公共层调度环槽（后端按 ring_cur 填句柄） */
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY && surf->ring_cur < 0) {
        int slot = surf->ring_acquire;
        int st = __atomic_load_n(&surf->ring_state[slot], __ATOMIC_ACQUIRE);
        if (st != _SC_GPU_RING_FREE) {
            _sc_gpu_log("frame_acquire: 环满（消费端未归还）");
            return 0;
        }
        surf->ring_state[slot] = _SC_GPU_RING_ACQUIRED;
        surf->ring_cur = slot;
        if (_sc_gpu.mem_acquired_count < 16)
            _sc_gpu.mem_acquired[_sc_gpu.mem_acquired_count++] = surf;
    }
    memset(out, 0, sizeof(*out));
    return _sc_gpu.api->frame_acquire(surf, out) ? 1 : 0;
}

void sc_gpu_frame_end(void) {
    if (!_sc_gpu.valid) return;
    _sc_gpu.api->frame_end();   /* 后端先收尾（swap/fence，ring_cur 仍有效） */
    /* MEMORY 环推进：本帧渲染完的槽 → RENDERED（消费线程可见） */
    for (int i = 0; i < _sc_gpu.mem_acquired_count; i++) {
        _sc_gpu_surface_t* surf = _sc_gpu.mem_acquired[i];
        if (surf->ring_cur >= 0) {
            __atomic_store_n(&surf->ring_state[surf->ring_cur],
                             _SC_GPU_RING_RENDERED, __ATOMIC_RELEASE);
            surf->ring_acquire = (surf->ring_acquire + 1) % surf->desc.image_count;
            surf->ring_cur = -1;
        }
    }
    _sc_gpu.mem_acquired_count = 0;
}

/* ---- memimg ------------------------------------------------ */

static void resolveMemimgDesc(sc_gpu_memimg_desc* d) {
    d->format = DEF(d->format, SC_GPU_PIXELFORMAT_BGRA8);
    if (!d->fourcc) d->fourcc = formatToFourcc(d->format);
}

sc_gpu_memimg sc_gpu_memimg_alloc(const sc_gpu_memimg_desc* desc) {
    if (!_sc_gpu.valid || !desc || desc->width <= 0 || desc->height <= 0) return 0;
    if (!_sc_gpu.api->memimg_alloc) {
        _sc_gpu_log("memimg_alloc: 后端 %s 不支持", _sc_gpu.api->name);
        return 0;
    }
    uint32_t id = poolAlloc(&_sc_gpu.memimg_pool);
    if (!id) { _sc_gpu_log("memimg_alloc: 池满"); return 0; }
    _sc_gpu_memimg_t* img = &_sc_gpu.memimgs[_sc_gpu_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->id = id;
    img->desc = *desc;
    resolveMemimgDesc(&img->desc);
    if (!img->desc.fourcc) {
        _sc_gpu_log("memimg_alloc: 格式不支持");
        img->id = 0;
        poolRelease(&_sc_gpu.memimg_pool, id);
        return 0;
    }
    img->state = _sc_gpu.api->memimg_alloc(img) ? _SC_GPU_SLOT_VALID
                                                : _SC_GPU_SLOT_FAILED;
    return id;
}

sc_gpu_memimg sc_gpu_memimg_import(const sc_gpu_memory_frame* src) {
    if (!_sc_gpu.valid || !src) return 0;
    if (!_sc_gpu.api->memimg_import) {
        _sc_gpu_log("memimg_import: 后端 %s 不支持", _sc_gpu.api->name);
        return 0;
    }
    uint32_t id = poolAlloc(&_sc_gpu.memimg_pool);
    if (!id) { _sc_gpu_log("memimg_import: 池满"); return 0; }
    _sc_gpu_memimg_t* img = &_sc_gpu.memimgs[_sc_gpu_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->id = id;
    img->imported = true;
    img->desc.width = src->width;
    img->desc.height = src->height;
    img->desc.fourcc = src->fourcc;
    img->state = _sc_gpu.api->memimg_import(img, src) ? _SC_GPU_SLOT_VALID
                                                      : _SC_GPU_SLOT_FAILED;
    return id;
}

int sc_gpu_memimg_export(sc_gpu_memimg hnd, sc_gpu_memory_frame* out, int with_fence) {
    if (!_sc_gpu.valid || !out) return 0;
    _sc_gpu_memimg_t* img = _sc_gpu_lookup_memimg(hnd);
    if (!img || img->state != _SC_GPU_SLOT_VALID || !_sc_gpu.api->memimg_export) return 0;
    memset(out, 0, sizeof(*out));
    out->sync_fd = -1;
    if (!_sc_gpu.api->memimg_export(img, out, with_fence != 0)) return 0;
    out->img = hnd;
    return 1;
}

void* sc_gpu_memimg_native(sc_gpu_memimg hnd) {
    if (!_sc_gpu.valid) return NULL;
    _sc_gpu_memimg_t* img = _sc_gpu_lookup_memimg(hnd);
    if (!img || img->state != _SC_GPU_SLOT_VALID || !_sc_gpu.api->memimg_native) return NULL;
    return _sc_gpu.api->memimg_native(img);
}

void* sc_gpu_memimg_map(sc_gpu_memimg hnd, int plane, uint32_t* out_stride) {
    if (!_sc_gpu.valid) return NULL;
    _sc_gpu_memimg_t* img = _sc_gpu_lookup_memimg(hnd);
    if (!img || img->state != _SC_GPU_SLOT_VALID || !_sc_gpu.api->memimg_map) return NULL;
    return _sc_gpu.api->memimg_map(img, plane, out_stride);
}

void sc_gpu_memimg_unmap(sc_gpu_memimg hnd, int plane) {
    if (!_sc_gpu.valid) return;
    _sc_gpu_memimg_t* img = _sc_gpu_lookup_memimg(hnd);
    if (!img || img->state != _SC_GPU_SLOT_VALID || !_sc_gpu.api->memimg_unmap) return;
    _sc_gpu.api->memimg_unmap(img, plane);
}

void sc_gpu_memimg_free(sc_gpu_memimg hnd) {
    if (!_sc_gpu.valid) return;
    _sc_gpu_memimg_t* img = _sc_gpu_lookup_memimg(hnd);
    if (!img) return;
    if (img->state == _SC_GPU_SLOT_VALID && _sc_gpu.api->memimg_free)
        _sc_gpu.api->memimg_free(img);
    img->id = 0;
    img->state = _SC_GPU_SLOT_FREE;
    poolRelease(&_sc_gpu.memimg_pool, hnd);
}

/* ---- MEMORY surface 消费端 ---------------------------------- */

int sc_gpu_memory_dequeue(sc_gpu_surface hnd, sc_gpu_memory_frame* out) {
    if (!_sc_gpu.valid || !out) return 0;
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf || surf->state != _SC_GPU_SLOT_VALID ||
        surf->desc.kind != SC_GPU_SURFACE_MEMORY) return 0;
    int slot = surf->ring_dequeue;
    int st = __atomic_load_n(&surf->ring_state[slot], __ATOMIC_ACQUIRE);
    if (st != _SC_GPU_RING_RENDERED) return 0;   /* 无成帧 */
    memset(out, 0, sizeof(*out));
    out->sync_fd = -1;
    if (!_sc_gpu.api->surface_dequeue ||
        !_sc_gpu.api->surface_dequeue(surf, slot, out)) return 0;
    out->img = surf->ring_imgs[slot];
    out->slot = (uint32_t)slot;
    __atomic_store_n(&surf->ring_state[slot], _SC_GPU_RING_DEQUEUED, __ATOMIC_RELEASE);
    surf->ring_dequeue = (surf->ring_dequeue + 1) % surf->desc.image_count;
    return 1;
}

void sc_gpu_memory_enqueue(sc_gpu_surface hnd, uint32_t slot) {
    if (!_sc_gpu.valid) return;
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf || surf->desc.kind != SC_GPU_SURFACE_MEMORY ||
        slot >= (uint32_t)surf->desc.image_count) return;
    int st = __atomic_load_n(&surf->ring_state[slot], __ATOMIC_ACQUIRE);
    if (st != _SC_GPU_RING_DEQUEUED) {
        _sc_gpu_log("memory_enqueue: 槽 %u 非消费中状态", slot);
        return;
    }
    __atomic_store_n(&surf->ring_state[slot], _SC_GPU_RING_FREE, __ATOMIC_RELEASE);
}
