/* ============================================================
 * gpu.c —— 公共层：句柄池、缺省值、校验、后端选择与分发
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

void _sc_gpu_pool_init(_sc_gpu_pool* p, int num) {
    p->size = num + 1;                       /* 槽 0 保留 */
    p->queue_top = 0;
    p->free_queue = (int*)calloc((size_t)num, sizeof(int));
    p->gen = (uint32_t*)calloc((size_t)p->size, sizeof(uint32_t));
    for (int i = num; i >= 1; i--)           /* 低索引先出 */
        p->free_queue[p->queue_top++] = i;
}

void _sc_gpu_pool_free(_sc_gpu_pool* p) {
    free(p->free_queue); p->free_queue = NULL;
    free(p->gen);        p->gen = NULL;
    p->size = 0; p->queue_top = 0;
}

uint32_t _sc_gpu_pool_alloc(_sc_gpu_pool* p) {
    if (p->queue_top <= 0) return 0;
    int index = p->free_queue[--p->queue_top];
    uint32_t gen = ++p->gen[index];
    if ((gen & 0xFFFF) == 0) gen = ++p->gen[index];   /* 代数跳过 0 */
    return ((gen & 0xFFFF) << 16) | (uint32_t)index;
}

void _sc_gpu_pool_release(_sc_gpu_pool* p, uint32_t id) {
    int index = _sc_gpu_slot_index(id);
    if (index <= 0 || index >= p->size) return;
    p->free_queue[p->queue_top++] = index;
}

/* ---- 查表 ------------------------------------------------- */

#define LOOKUP_IMPL(name, poolfield, arrfield, type)                          \
    type* _sc_gpu_lookup_##name(uint32_t id) {                                \
        if (!id) return NULL;                                                 \
        int index = _sc_gpu_slot_index(id);                                   \
        if (index <= 0 || index >= _sc_gpu.poolfield.size) return NULL;       \
        type* res = &_sc_gpu.arrfield[index];                                 \
        if (res->slot.id != id) return NULL;                                  \
        return res;                                                           \
    }

LOOKUP_IMPL(buffer,   buffer_pool,   buffers,   _sc_gpu_buffer_t)
LOOKUP_IMPL(image,    image_pool,    images,    _sc_gpu_image_t)
LOOKUP_IMPL(sampler,  sampler_pool,  samplers,  _sc_gpu_sampler_t)
LOOKUP_IMPL(shader,   shader_pool,   shaders,   _sc_gpu_shader_t)
LOOKUP_IMPL(pipeline, pipeline_pool, pipelines, _sc_gpu_pipeline_t)
LOOKUP_IMPL(surface,  surface_pool,  surfaces,  _sc_gpu_surface_t)

/* ---- 缺省值 ----------------------------------------------- */

#define DEF(v, d) ((v) == 0 ? (d) : (v))

static void resolveSurfaceDesc(sc_gpu_surface_desc* d) {
    d->color_format  = DEF(d->color_format, SC_GPU_PIXELFORMAT_BGRA8);
    d->depth_format  = DEF(d->depth_format, SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
    d->sample_count  = DEF(d->sample_count, 1);
    d->swap_interval = DEF(d->swap_interval, 1);
}

static sc_gpu_desc resolveDesc(const sc_gpu_desc* in) {
    sc_gpu_desc d = *in;
    resolveSurfaceDesc(&d.surface);
    d.buffer_pool_size   = DEF(d.buffer_pool_size, 128);
    d.image_pool_size    = DEF(d.image_pool_size, 128);
    d.sampler_pool_size  = DEF(d.sampler_pool_size, 64);
    d.shader_pool_size   = DEF(d.shader_pool_size, 64);
    d.pipeline_pool_size = DEF(d.pipeline_pool_size, 64);
    d.surface_pool_size  = DEF(d.surface_pool_size, 8);
    return d;
}

static void resolveBufferDesc(sc_gpu_buffer_desc* d) {
    d->kind  = DEF(d->kind, SC_GPU_BUFFERKIND_VERTEX);
    d->usage = DEF(d->usage, SC_GPU_USAGE_IMMUTABLE);
    if (d->size == 0) d->size = d->data.size;
}

static void resolveImageDesc(sc_gpu_image_desc* d) {
    d->kind      = DEF(d->kind, SC_GPU_IMAGEKIND_2D);
    d->slices    = DEF(d->slices, 1);
    d->mip_count = DEF(d->mip_count, 1);
    d->format    = DEF(d->format, SC_GPU_PIXELFORMAT_RGBA8);
    d->usage     = DEF(d->usage, SC_GPU_USAGE_IMMUTABLE);
    d->sample_count = DEF(d->sample_count, 1);
}

static void resolveSamplerDesc(sc_gpu_sampler_desc* d) {
    d->min_filter    = DEF(d->min_filter, SC_GPU_FILTER_NEAREST);
    d->mag_filter    = DEF(d->mag_filter, SC_GPU_FILTER_NEAREST);
    d->mipmap_filter = DEF(d->mipmap_filter, SC_GPU_FILTER_NEAREST);
    d->wrap_u = DEF(d->wrap_u, SC_GPU_WRAP_REPEAT);
    d->wrap_v = DEF(d->wrap_v, SC_GPU_WRAP_REPEAT);
    d->wrap_w = DEF(d->wrap_w, SC_GPU_WRAP_REPEAT);
    d->compare = DEF(d->compare, SC_GPU_COMPARE_ALWAYS);
}

/* 顶点格式字节宽 */
static int vertexFormatSize(sc_gpu_vertex_format f) {
    switch (f) {
        case SC_GPU_VERTEXFORMAT_FLOAT:  return 4;
        case SC_GPU_VERTEXFORMAT_FLOAT2: return 8;
        case SC_GPU_VERTEXFORMAT_FLOAT3: return 12;
        case SC_GPU_VERTEXFORMAT_FLOAT4: return 16;
        case SC_GPU_VERTEXFORMAT_BYTE4: case SC_GPU_VERTEXFORMAT_BYTE4N:
        case SC_GPU_VERTEXFORMAT_UBYTE4: case SC_GPU_VERTEXFORMAT_UBYTE4N:
        case SC_GPU_VERTEXFORMAT_SHORT2: case SC_GPU_VERTEXFORMAT_SHORT2N:
        case SC_GPU_VERTEXFORMAT_USHORT2: case SC_GPU_VERTEXFORMAT_USHORT2N:
        case SC_GPU_VERTEXFORMAT_HALF2: case SC_GPU_VERTEXFORMAT_UINT10N2:
        case SC_GPU_VERTEXFORMAT_UINT:   return 4;
        case SC_GPU_VERTEXFORMAT_SHORT4: case SC_GPU_VERTEXFORMAT_SHORT4N:
        case SC_GPU_VERTEXFORMAT_USHORT4: case SC_GPU_VERTEXFORMAT_USHORT4N:
        case SC_GPU_VERTEXFORMAT_HALF4: return 8;
        default: return 0;
    }
}

static void resolvePipelineDesc(sc_gpu_pipeline_desc* d) {
    /* 交换链缺省参照：当前 surface（无则取 init 的默认 surface desc） */
    const sc_gpu_surface_desc* sd = _sc_gpu.cur_surface ? &_sc_gpu.cur_surface->desc
                                                        : &_sc_gpu.desc.surface;
    d->primitive  = DEF(d->primitive, SC_GPU_PRIMITIVE_TRIANGLES);
    d->index_type = DEF(d->index_type, SC_GPU_INDEXTYPE_NONE);
    d->cull       = DEF(d->cull, SC_GPU_CULL_NONE);
    d->winding    = DEF(d->winding, SC_GPU_WINDING_CCW);
    d->color_count = DEF(d->color_count, 1);
    d->sample_count = DEF(d->sample_count, sd->sample_count);
    d->depth.compare = DEF(d->depth.compare, SC_GPU_COMPARE_ALWAYS);
    if (d->depth.format == 0) d->depth.format = sd->depth_format;
    if (d->stencil.enabled) {
        sc_gpu_stencil_face_state* faces[2] = { &d->stencil.front, &d->stencil.back };
        for (int i = 0; i < 2; i++) {
            faces[i]->compare       = DEF(faces[i]->compare, SC_GPU_COMPARE_ALWAYS);
            faces[i]->fail_op       = DEF(faces[i]->fail_op, SC_GPU_STENCILOP_KEEP);
            faces[i]->depth_fail_op = DEF(faces[i]->depth_fail_op, SC_GPU_STENCILOP_KEEP);
            faces[i]->pass_op       = DEF(faces[i]->pass_op, SC_GPU_STENCILOP_KEEP);
        }
        d->stencil.read_mask  = DEF(d->stencil.read_mask, 0xFF);
        d->stencil.write_mask = DEF(d->stencil.write_mask, 0xFF);
    }
    for (int i = 0; i < d->color_count; i++) {
        sc_gpu_color_target_state* c = &d->colors[i];
        c->format = DEF(c->format, sd->color_format);
        if (c->blend.enabled) {
            c->blend.src_factor_rgb   = DEF(c->blend.src_factor_rgb, SC_GPU_BLEND_ONE);
            c->blend.dst_factor_rgb   = DEF(c->blend.dst_factor_rgb, SC_GPU_BLEND_ZERO);
            c->blend.op_rgb           = DEF(c->blend.op_rgb, SC_GPU_BLENDOP_ADD);
            c->blend.src_factor_alpha = DEF(c->blend.src_factor_alpha, c->blend.src_factor_rgb);
            c->blend.dst_factor_alpha = DEF(c->blend.dst_factor_alpha, c->blend.dst_factor_rgb);
            c->blend.op_alpha         = DEF(c->blend.op_alpha, c->blend.op_rgb);
        }
    }
    /* 顶点布局：自动 stride / offset（同 sokol：offset 0 按声明序累加） */
    int auto_offset[SC_GPU_MAX_VERTEX_BUFFERS] = {0};
    bool use_auto = true;
    for (int i = 0; i < SC_GPU_MAX_VERTEX_ATTRS; i++)
        if (d->attrs[i].format != SC_GPU_VERTEXFORMAT_INVALID && d->attrs[i].offset != 0)
            use_auto = false;
    for (int i = 0; i < SC_GPU_MAX_VERTEX_ATTRS; i++) {
        sc_gpu_vertex_attr* a = &d->attrs[i];
        if (a->format == SC_GPU_VERTEXFORMAT_INVALID) continue;
        if (use_auto) {
            a->offset = auto_offset[a->buffer_index];
            auto_offset[a->buffer_index] += vertexFormatSize(a->format);
        }
    }
    for (int i = 0; i < SC_GPU_MAX_VERTEX_BUFFERS; i++)
        if (d->buffers[i].stride == 0)
            d->buffers[i].stride = auto_offset[i];
}

static void resolvePassAction(sc_gpu_pass_action* a, int color_count,
                              const bool has_resolve[SC_GPU_MAX_COLOR_ATTACHMENTS]) {
    for (int i = 0; i < color_count; i++) {
        a->colors[i].load  = DEF(a->colors[i].load, SC_GPU_LOADACTION_CLEAR);
        a->colors[i].store = DEF(a->colors[i].store,
            (has_resolve && has_resolve[i]) ? SC_GPU_STOREACTION_RESOLVE
                                            : SC_GPU_STOREACTION_STORE);
    }
    a->depth.load  = DEF(a->depth.load, SC_GPU_LOADACTION_CLEAR);
    a->depth.store = DEF(a->depth.store, SC_GPU_STOREACTION_DONTCARE);
    if (a->depth.clear_depth == 0.0f) a->depth.clear_depth = 1.0f;
}

/* ---- 后端选择 --------------------------------------------- */

static const _sc_gpu_backend_api* pickBackend(sc_gpu_backend want) {
    switch (want) {
        case SC_GPU_BACKEND_METAL:
#ifdef SC_GPU_METAL
            return _sc_gpu_backend_metal();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_GL:
#ifdef SC_GPU_GL
            return _sc_gpu_backend_gl();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_NULL:
            return _sc_gpu_backend_null();
        case SC_GPU_BACKEND_DEFAULT:
        default:
            break;
    }
    /* 平台默认：按编入优先级 */
#ifdef SC_GPU_METAL
    return _sc_gpu_backend_metal();
#elif defined(SC_GPU_GL)
    return _sc_gpu_backend_gl();
#else
    return _sc_gpu_backend_null();
#endif
}

/* ---- 生命周期 --------------------------------------------- */

int sc_gpu_init(const sc_gpu_desc* desc) {
    if (_sc_gpu.valid) { _sc_gpu_log("init: 已初始化"); return 0; }
    memset(&_sc_gpu, 0, sizeof(_sc_gpu));
    _sc_gpu.desc = resolveDesc(desc);

    const _sc_gpu_backend_api* api = pickBackend(_sc_gpu.desc.backend);
    if (!api) { _sc_gpu_log("init: 请求的后端未编入本库"); return 0; }

    _sc_gpu_pool_init(&_sc_gpu.buffer_pool,   _sc_gpu.desc.buffer_pool_size);
    _sc_gpu_pool_init(&_sc_gpu.image_pool,    _sc_gpu.desc.image_pool_size);
    _sc_gpu_pool_init(&_sc_gpu.sampler_pool,  _sc_gpu.desc.sampler_pool_size);
    _sc_gpu_pool_init(&_sc_gpu.shader_pool,   _sc_gpu.desc.shader_pool_size);
    _sc_gpu_pool_init(&_sc_gpu.pipeline_pool, _sc_gpu.desc.pipeline_pool_size);
    _sc_gpu_pool_init(&_sc_gpu.surface_pool,  _sc_gpu.desc.surface_pool_size);
    _sc_gpu.buffers   = (_sc_gpu_buffer_t*)  calloc((size_t)_sc_gpu.buffer_pool.size,   sizeof(_sc_gpu_buffer_t));
    _sc_gpu.images    = (_sc_gpu_image_t*)   calloc((size_t)_sc_gpu.image_pool.size,    sizeof(_sc_gpu_image_t));
    _sc_gpu.samplers  = (_sc_gpu_sampler_t*) calloc((size_t)_sc_gpu.sampler_pool.size,  sizeof(_sc_gpu_sampler_t));
    _sc_gpu.shaders   = (_sc_gpu_shader_t*)  calloc((size_t)_sc_gpu.shader_pool.size,   sizeof(_sc_gpu_shader_t));
    _sc_gpu.pipelines = (_sc_gpu_pipeline_t*)calloc((size_t)_sc_gpu.pipeline_pool.size, sizeof(_sc_gpu_pipeline_t));
    _sc_gpu.surfaces  = (_sc_gpu_surface_t*) calloc((size_t)_sc_gpu.surface_pool.size,  sizeof(_sc_gpu_surface_t));

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
        /* 逐资源销毁（后端仍活着时） */
        for (int i = 1; i < _sc_gpu.pipeline_pool.size; i++)
            if (_sc_gpu.pipelines[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->pipeline_destroy(&_sc_gpu.pipelines[i]);
        for (int i = 1; i < _sc_gpu.shader_pool.size; i++)
            if (_sc_gpu.shaders[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->shader_destroy(&_sc_gpu.shaders[i]);
        for (int i = 1; i < _sc_gpu.sampler_pool.size; i++)
            if (_sc_gpu.samplers[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->sampler_destroy(&_sc_gpu.samplers[i]);
        for (int i = 1; i < _sc_gpu.image_pool.size; i++)
            if (_sc_gpu.images[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->image_destroy(&_sc_gpu.images[i]);
        for (int i = 1; i < _sc_gpu.buffer_pool.size; i++)
            if (_sc_gpu.buffers[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->buffer_destroy(&_sc_gpu.buffers[i]);
        for (int i = 1; i < _sc_gpu.surface_pool.size; i++)
            if (_sc_gpu.surfaces[i].slot.state == _SC_GPU_SLOT_VALID)
                _sc_gpu.api->surface_destroy(&_sc_gpu.surfaces[i]);
        _sc_gpu.api->shutdown();
    }
    free(_sc_gpu.buffers);   free(_sc_gpu.images); free(_sc_gpu.samplers);
    free(_sc_gpu.shaders);   free(_sc_gpu.pipelines); free(_sc_gpu.surfaces);
    _sc_gpu_pool_free(&_sc_gpu.buffer_pool);
    _sc_gpu_pool_free(&_sc_gpu.image_pool);
    _sc_gpu_pool_free(&_sc_gpu.sampler_pool);
    _sc_gpu_pool_free(&_sc_gpu.shader_pool);
    _sc_gpu_pool_free(&_sc_gpu.pipeline_pool);
    _sc_gpu_pool_free(&_sc_gpu.surface_pool);
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
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.surface_pool);
    if (!id) { _sc_gpu_log("make_surface: 池满"); return 0; }
    _sc_gpu_surface_t* surf = &_sc_gpu.surfaces[_sc_gpu_slot_index(id)];
    memset(surf, 0, sizeof(*surf));
    surf->slot.id = id;
    surf->desc = *desc;
    resolveSurfaceDesc(&surf->desc);
    surf->slot.state = _sc_gpu.api->surface_create(surf) ? _SC_GPU_SLOT_VALID
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
    if (surf->slot.state == _SC_GPU_SLOT_VALID) _sc_gpu.api->surface_destroy(surf);
    surf->slot.id = 0;
    surf->slot.state = _SC_GPU_SLOT_FREE;
    _sc_gpu_pool_release(&_sc_gpu.surface_pool, hnd);
}

void sc_gpu_make_current(sc_gpu_surface hnd) {
    if (!_sc_gpu.valid) return;
    if (_sc_gpu.in_pass) { _sc_gpu_log("make_current: pass 内不可切换 surface"); return; }
    _sc_gpu_surface_t* surf = _sc_gpu_lookup_surface(hnd);
    if (!surf || surf->slot.state != _SC_GPU_SLOT_VALID) {
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
    if (!surf || surf->slot.state != _SC_GPU_SLOT_VALID) return;
    if (surf->desc.width == width && surf->desc.height == height) return;
    surf->desc.width = width;
    surf->desc.height = height;
    _sc_gpu.api->surface_resize(surf, width, height);
}

/* ---- 能力查询 ----------------------------------------------- */

sc_gpu_pixelformat_info sc_gpu_query_pixelformat(sc_gpu_pixel_format fmt) {
    sc_gpu_pixelformat_info info;
    memset(&info, 0, sizeof(info));
    if (_sc_gpu.valid && _sc_gpu.api->query_pixelformat)
        _sc_gpu.api->query_pixelformat(fmt, &info);
    return info;
}

/* ---- 资源创建/销毁 ----------------------------------------- */

sc_gpu_buffer sc_gpu_make_buffer(const sc_gpu_buffer_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.buffer_pool);
    if (!id) { _sc_gpu_log("make_buffer: 池满"); return 0; }
    _sc_gpu_buffer_t* buf = &_sc_gpu.buffers[_sc_gpu_slot_index(id)];
    memset(buf, 0, sizeof(*buf));
    buf->slot.id = id;
    buf->desc = *desc;
    resolveBufferDesc(&buf->desc);
    if (buf->desc.usage == SC_GPU_USAGE_IMMUTABLE && !buf->desc.data.ptr) {
        _sc_gpu_log("make_buffer: IMMUTABLE 须提供初始数据");
        buf->slot.state = _SC_GPU_SLOT_FAILED;
    } else {
        buf->slot.state = _sc_gpu.api->buffer_create(buf) ? _SC_GPU_SLOT_VALID
                                                          : _SC_GPU_SLOT_FAILED;
    }
    return id;
}

sc_gpu_image sc_gpu_make_image(const sc_gpu_image_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.image_pool);
    if (!id) { _sc_gpu_log("make_image: 池满"); return 0; }
    _sc_gpu_image_t* img = &_sc_gpu.images[_sc_gpu_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->slot.id = id;
    img->desc = *desc;
    resolveImageDesc(&img->desc);
    img->slot.state = _sc_gpu.api->image_create(img) ? _SC_GPU_SLOT_VALID
                                                     : _SC_GPU_SLOT_FAILED;
    return id;
}

sc_gpu_sampler sc_gpu_make_sampler(const sc_gpu_sampler_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.sampler_pool);
    if (!id) { _sc_gpu_log("make_sampler: 池满"); return 0; }
    _sc_gpu_sampler_t* smp = &_sc_gpu.samplers[_sc_gpu_slot_index(id)];
    memset(smp, 0, sizeof(*smp));
    smp->slot.id = id;
    smp->desc = *desc;
    resolveSamplerDesc(&smp->desc);
    smp->slot.state = _sc_gpu.api->sampler_create(smp) ? _SC_GPU_SLOT_VALID
                                                       : _SC_GPU_SLOT_FAILED;
    return id;
}

sc_gpu_shader sc_gpu_make_shader(const sc_gpu_shader_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.shader_pool);
    if (!id) { _sc_gpu_log("make_shader: 池满"); return 0; }
    _sc_gpu_shader_t* shd = &_sc_gpu.shaders[_sc_gpu_slot_index(id)];
    memset(shd, 0, sizeof(*shd));
    shd->slot.id = id;
    shd->has_cs = desc->cs.code.ptr != NULL;

    /* 反射清单优先；无清单时收手工绑定描述 */
    if (desc->reflect_json) {
        if (!_sc_gpu_parse_reflect(desc->reflect_json, &shd->reflect)) {
            _sc_gpu_log("make_shader: 反射清单解析失败");
            shd->slot.state = _SC_GPU_SLOT_FAILED;
            return id;
        }
    } else {
        _sc_gpu_reflect* r = &shd->reflect;
        for (int i = 0; i < SC_GPU_MAX_UNIFORM_BLOCKS * 2; i++) {
            const sc_gpu_uniform_block_desc* ub = &desc->uniform_blocks[i];
            if (ub->size == 0) continue;
            _sc_gpu_reflect_block* b = &r->blocks[r->block_count++];
            b->stage = ub->stage; b->slot = ub->slot; b->size = ub->size;
            if (ub->name) { strncpy(b->name, ub->name, sizeof(b->name) - 1); }
        }
        for (int i = 0; i < SC_GPU_MAX_IMAGES; i++) {
            const sc_gpu_image_slot_desc* is = &desc->images[i];
            if (!is->name) continue;
            _sc_gpu_reflect_sampler* s = &r->samplers[r->sampler_count++];
            s->stage = is->stage; s->slot = is->slot;
            strncpy(s->name, is->name, sizeof(s->name) - 1);
        }
    }
    shd->slot.state = _sc_gpu.api->shader_create(shd, desc) ? _SC_GPU_SLOT_VALID
                                                            : _SC_GPU_SLOT_FAILED;
    return id;
}

sc_gpu_pipeline sc_gpu_make_pipeline(const sc_gpu_pipeline_desc* desc) {
    if (!_sc_gpu.valid || !desc) return 0;
    _sc_gpu_shader_t* shd = _sc_gpu_lookup_shader(desc->shader);
    if (!shd || shd->slot.state != _SC_GPU_SLOT_VALID) {
        _sc_gpu_log("make_pipeline: 无效 shader");
        return 0;
    }
    uint32_t id = _sc_gpu_pool_alloc(&_sc_gpu.pipeline_pool);
    if (!id) { _sc_gpu_log("make_pipeline: 池满"); return 0; }
    _sc_gpu_pipeline_t* pip = &_sc_gpu.pipelines[_sc_gpu_slot_index(id)];
    memset(pip, 0, sizeof(*pip));
    pip->slot.id = id;
    pip->desc = *desc;
    pip->shader = shd;
    resolvePipelineDesc(&pip->desc);
    pip->slot.state = _sc_gpu.api->pipeline_create(pip) ? _SC_GPU_SLOT_VALID
                                                        : _SC_GPU_SLOT_FAILED;
    return id;
}

#define DESTROY_IMPL(name, poolfield, type, apifn)                             \
    void sc_gpu_destroy_##name(type hnd) {                                     \
        if (!_sc_gpu.valid) return;                                            \
        _sc_gpu_##name##_t* res = _sc_gpu_lookup_##name(hnd);                  \
        if (!res) return;                                                      \
        if (res->slot.state == _SC_GPU_SLOT_VALID) _sc_gpu.api->apifn(res);    \
        res->slot.id = 0;                                                      \
        res->slot.state = _SC_GPU_SLOT_FREE;                                   \
        _sc_gpu_pool_release(&_sc_gpu.poolfield, hnd);                         \
    }

DESTROY_IMPL(buffer,   buffer_pool,   sc_gpu_buffer,   buffer_destroy)
DESTROY_IMPL(image,    image_pool,    sc_gpu_image,    image_destroy)
DESTROY_IMPL(sampler,  sampler_pool,  sc_gpu_sampler,  sampler_destroy)
DESTROY_IMPL(shader,   shader_pool,   sc_gpu_shader,   shader_destroy)
DESTROY_IMPL(pipeline, pipeline_pool, sc_gpu_pipeline, pipeline_destroy)

/* ---- 资源更新 --------------------------------------------- */

void sc_gpu_update_buffer(sc_gpu_buffer hnd, const sc_gpu_range* data) {
    if (!_sc_gpu.valid || !data || !data->ptr) return;
    _sc_gpu_buffer_t* buf = _sc_gpu_lookup_buffer(hnd);
    if (!buf || buf->slot.state != _SC_GPU_SLOT_VALID) return;
    if (buf->desc.usage == SC_GPU_USAGE_IMMUTABLE) {
        _sc_gpu_log("update_buffer: IMMUTABLE 不可更新"); return;
    }
    if (data->size > buf->desc.size) {
        _sc_gpu_log("update_buffer: 数据超出缓冲大小"); return;
    }
    _sc_gpu.api->buffer_update(buf, data, 0);
}

int sc_gpu_append_buffer(sc_gpu_buffer hnd, const sc_gpu_range* data) {
    if (!_sc_gpu.valid || !data || !data->ptr) return 0;
    _sc_gpu_buffer_t* buf = _sc_gpu_lookup_buffer(hnd);
    if (!buf || buf->slot.state != _SC_GPU_SLOT_VALID) return 0;
    /* 按 4 字节对齐追加 */
    int offset = (buf->append_pos + 3) & ~3;
    if ((size_t)offset + data->size > buf->desc.size) {
        buf->append_overflow = true;
        _sc_gpu_log("append_buffer: 溢出");
        return offset;
    }
    _sc_gpu.api->buffer_update(buf, data, offset);
    buf->append_pos = offset + (int)data->size;
    return offset;
}

void sc_gpu_update_image(sc_gpu_image hnd, const sc_gpu_image_data* data) {
    if (!_sc_gpu.valid || !data) return;
    _sc_gpu_image_t* img = _sc_gpu_lookup_image(hnd);
    if (!img || img->slot.state != _SC_GPU_SLOT_VALID) return;
    if (img->desc.usage == SC_GPU_USAGE_IMMUTABLE) {
        _sc_gpu_log("update_image: IMMUTABLE 不可更新"); return;
    }
    _sc_gpu.api->image_update(img, data);
}

/* ---- 状态查询 --------------------------------------------- */

static int slotToState(_sc_gpu_slot_state s) {
    switch (s) {
        case _SC_GPU_SLOT_VALID:  return SC_GPU_RESOURCESTATE_VALID;
        case _SC_GPU_SLOT_FAILED: return SC_GPU_RESOURCESTATE_FAILED;
        case _SC_GPU_SLOT_ALLOC:  return SC_GPU_RESOURCESTATE_INITIAL;
        default:                  return SC_GPU_RESOURCESTATE_INVALID;
    }
}

int sc_gpu_query_buffer_state(sc_gpu_buffer h) {
    _sc_gpu_buffer_t* r = _sc_gpu_lookup_buffer(h);
    return r ? slotToState(r->slot.state) : SC_GPU_RESOURCESTATE_INVALID;
}
int sc_gpu_query_image_state(sc_gpu_image h) {
    _sc_gpu_image_t* r = _sc_gpu_lookup_image(h);
    return r ? slotToState(r->slot.state) : SC_GPU_RESOURCESTATE_INVALID;
}
int sc_gpu_query_shader_state(sc_gpu_shader h) {
    _sc_gpu_shader_t* r = _sc_gpu_lookup_shader(h);
    return r ? slotToState(r->slot.state) : SC_GPU_RESOURCESTATE_INVALID;
}
int sc_gpu_query_pipeline_state(sc_gpu_pipeline h) {
    _sc_gpu_pipeline_t* r = _sc_gpu_lookup_pipeline(h);
    return r ? slotToState(r->slot.state) : SC_GPU_RESOURCESTATE_INVALID;
}

/* ---- 帧 --------------------------------------------------- */

void sc_gpu_begin_pass(const sc_gpu_pass* pass) {
    if (!_sc_gpu.valid || !pass) return;
    if (_sc_gpu.in_pass) { _sc_gpu_log("begin_pass: pass 嵌套"); return; }

    sc_gpu_pass p = *pass;
    _sc_gpu_image_t* colors[SC_GPU_MAX_COLOR_ATTACHMENTS] = {0};
    _sc_gpu_image_t* resolves[SC_GPU_MAX_COLOR_ATTACHMENTS] = {0};
    bool has_resolve[SC_GPU_MAX_COLOR_ATTACHMENTS] = {0};
    _sc_gpu_image_t* depth = NULL;
    int color_count = 0;
    for (int i = 0; i < SC_GPU_MAX_COLOR_ATTACHMENTS; i++) {
        if (!p.colors[i].image) break;
        colors[i] = _sc_gpu_lookup_image(p.colors[i].image);
        if (!colors[i] || colors[i]->slot.state != _SC_GPU_SLOT_VALID) {
            _sc_gpu_log("begin_pass: 无效颜色附件 %d", i); return;
        }
        if (p.resolves[i].image) {
            resolves[i] = _sc_gpu_lookup_image(p.resolves[i].image);
            if (!resolves[i] || resolves[i]->slot.state != _SC_GPU_SLOT_VALID) {
                _sc_gpu_log("begin_pass: 无效 resolve 附件 %d", i); return;
            }
            has_resolve[i] = true;
        }
        color_count++;
    }
    if (p.depth_stencil.image) {
        depth = _sc_gpu_lookup_image(p.depth_stencil.image);
        if (!depth || depth->slot.state != _SC_GPU_SLOT_VALID) {
            _sc_gpu_log("begin_pass: 无效深度附件"); return;
        }
    }
    _sc_gpu.pass_is_swapchain = (color_count == 0 && !p.compute);
    if (_sc_gpu.pass_is_swapchain && !_sc_gpu.cur_surface) {
        _sc_gpu_log("begin_pass: 无当前 surface（先 make_current）"); return;
    }
    /* 交换链 MSAA：后端自行解析到 drawable，store 默认 RESOLVE */
    bool sc_resolve[SC_GPU_MAX_COLOR_ATTACHMENTS] = {0};
    if (_sc_gpu.pass_is_swapchain && _sc_gpu.cur_surface->desc.sample_count > 1)
        sc_resolve[0] = true;
    resolvePassAction(&p.action, _sc_gpu.pass_is_swapchain ? 1 : color_count,
                      _sc_gpu.pass_is_swapchain ? sc_resolve : has_resolve);
    _sc_gpu.in_pass = true;
    _sc_gpu.cur_pipeline = NULL;
    _sc_gpu.api->begin_pass(&p, colors, color_count, resolves, depth);
}

void sc_gpu_apply_viewport(int x, int y, int w, int h, int origin_top_left) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass) return;
    _sc_gpu.api->apply_viewport(x, y, w, h, origin_top_left != 0);
}

void sc_gpu_apply_scissor(int x, int y, int w, int h, int origin_top_left) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass) return;
    _sc_gpu.api->apply_scissor(x, y, w, h, origin_top_left != 0);
}

void sc_gpu_apply_pipeline(sc_gpu_pipeline hnd) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass) return;
    _sc_gpu_pipeline_t* pip = _sc_gpu_lookup_pipeline(hnd);
    if (!pip || pip->slot.state != _SC_GPU_SLOT_VALID) {
        _sc_gpu_log("apply_pipeline: 无效管线"); return;
    }
    _sc_gpu.cur_pipeline = pip;
    _sc_gpu.api->apply_pipeline(pip);
}

void sc_gpu_apply_bindings(const sc_gpu_bindings* bnd) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass || !bnd) return;
    if (!_sc_gpu.cur_pipeline) { _sc_gpu_log("apply_bindings: 未 apply_pipeline"); return; }

    _sc_gpu_buffer_t* vbufs[SC_GPU_MAX_VERTEX_BUFFERS] = {0};
    _sc_gpu_buffer_t* ibuf = NULL;
    /* [0]=vs [1]=fs [2]=cs */
    _sc_gpu_image_t*   imgs[3][SC_GPU_MAX_IMAGES] = {{0}};
    _sc_gpu_sampler_t* smps[3][SC_GPU_MAX_SAMPLERS] = {{0}};
    _sc_gpu_buffer_t*  sbufs[3][SC_GPU_MAX_STORAGE_BUFFERS] = {{0}};

    for (int i = 0; i < SC_GPU_MAX_VERTEX_BUFFERS; i++) {
        if (!bnd->vertex_buffers[i]) continue;
        vbufs[i] = _sc_gpu_lookup_buffer(bnd->vertex_buffers[i]);
        if (!vbufs[i]) { _sc_gpu_log("apply_bindings: 无效顶点缓冲 %d", i); return; }
    }
    if (bnd->index_buffer) {
        ibuf = _sc_gpu_lookup_buffer(bnd->index_buffer);
        if (!ibuf) { _sc_gpu_log("apply_bindings: 无效索引缓冲"); return; }
    }
    const sc_gpu_stage_bindings* stages[3] = { &bnd->vs, &bnd->fs, &bnd->cs };
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < SC_GPU_MAX_IMAGES; i++)
            if (stages[s]->images[i])
                imgs[s][i] = _sc_gpu_lookup_image(stages[s]->images[i]);
        for (int i = 0; i < SC_GPU_MAX_SAMPLERS; i++)
            if (stages[s]->samplers[i])
                smps[s][i] = _sc_gpu_lookup_sampler(stages[s]->samplers[i]);
        for (int i = 0; i < SC_GPU_MAX_STORAGE_BUFFERS; i++)
            if (stages[s]->storage_buffers[i])
                sbufs[s][i] = _sc_gpu_lookup_buffer(stages[s]->storage_buffers[i]);
    }
    _sc_gpu.api->apply_bindings(_sc_gpu.cur_pipeline, bnd, vbufs, ibuf, imgs, smps, sbufs);
}

void sc_gpu_apply_uniforms(int stage, int slot, const void* data, uint64_t size) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass || !data || !size) return;
    _sc_gpu.api->apply_uniforms(stage, slot, data, (size_t)size);
}

void sc_gpu_draw(int base_element, int element_count, int instance_count) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass || !_sc_gpu.cur_pipeline) return;
    if (instance_count <= 0) instance_count = 1;
    _sc_gpu.api->draw(base_element, element_count, instance_count);
}

void sc_gpu_dispatch(int gx, int gy, int gz) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass || !_sc_gpu.cur_pipeline) return;
    if (!_sc_gpu.cur_pipeline->desc.compute) {
        _sc_gpu_log("dispatch: 当前非计算管线"); return;
    }
    _sc_gpu.api->dispatch(gx, gy, gz);
}

void sc_gpu_end_pass(void) {
    if (!_sc_gpu.valid || !_sc_gpu.in_pass) return;
    _sc_gpu.api->end_pass();
    _sc_gpu.in_pass = false;
    _sc_gpu.cur_pipeline = NULL;
}

void sc_gpu_commit(void) {
    if (!_sc_gpu.valid) return;
    _sc_gpu.api->commit();
    _sc_gpu.frame_index++;
    /* 每帧重置 append 位置（stream 语义） */
    for (int i = 1; i < _sc_gpu.buffer_pool.size; i++) {
        _sc_gpu_buffer_t* b = &_sc_gpu.buffers[i];
        if (b->slot.state == _SC_GPU_SLOT_VALID && b->desc.usage == SC_GPU_USAGE_STREAM) {
            b->append_pos = 0;
            b->append_overflow = false;
        }
    }
}
