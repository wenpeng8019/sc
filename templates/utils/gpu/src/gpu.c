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

/* ---- 缺省值 ----------------------------------------------- */

#define DEF(v, d) ((v) == 0 ? (d) : (v))

static void resolveSurfaceDesc(sc_gpu_surface_desc* d) {
    d->color_format  = DEF(d->color_format, SC_GPU_PIXELFORMAT_BGRA8);
    d->depth_format  = DEF(d->depth_format, SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
    d->sample_count  = DEF(d->sample_count, 1);
    d->swap_interval = DEF(d->swap_interval, 1);
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

    const _sc_gpu_env_api* api = pickBackend(_sc_gpu.desc.backend);
    if (!api) { _sc_gpu_log("init: 请求的后端未编入本库"); return 0; }

    poolInit(&_sc_gpu.surface_pool, _sc_gpu.desc.surface_pool_size);
    _sc_gpu.surfaces = (_sc_gpu_surface_t*)calloc(
        (size_t)_sc_gpu.surface_pool.size, sizeof(_sc_gpu_surface_t));

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
                _sc_gpu.api->surface_destroy(&_sc_gpu.surfaces[i]);
        _sc_gpu.api->shutdown();
    }
    free(_sc_gpu.surfaces);
    poolFree(&_sc_gpu.surface_pool);
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
    if (!_sc_gpu.valid || !desc || !desc->native_window) return 0;
    uint32_t id = poolAlloc(&_sc_gpu.surface_pool);
    if (!id) { _sc_gpu_log("make_surface: 池满"); return 0; }
    _sc_gpu_surface_t* surf = &_sc_gpu.surfaces[_sc_gpu_slot_index(id)];
    memset(surf, 0, sizeof(*surf));
    surf->id = id;
    surf->desc = *desc;
    resolveSurfaceDesc(&surf->desc);
    surf->state = _sc_gpu.api->surface_create(surf) ? _SC_GPU_SLOT_VALID
                                                    : _SC_GPU_SLOT_FAILED;
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
    if (!_sc_gpu.cur_surface) {
        _sc_gpu_log("frame_acquire: 无当前 surface（先 make_current）");
        return 0;
    }
    memset(out, 0, sizeof(*out));
    return _sc_gpu.api->frame_acquire(_sc_gpu.cur_surface, out) ? 1 : 0;
}

void sc_gpu_frame_end(void) {
    if (!_sc_gpu.valid) return;
    _sc_gpu.api->frame_end();
}
