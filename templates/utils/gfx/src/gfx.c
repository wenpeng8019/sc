/* ============================================================
 * gfx.c —— 公共层：句柄池、缺省值、校验、后端选择与分发
 * ============================================================
 * 后端种类跟随 gpu（env 层）：sc_gfx_init 按 sc_gpu_query_backend()
 * 选配对的命令翻译 vtable（Metal / GL / Null），不独立选择。
 * ============================================================ */

#include "internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

gfx_state g_gfx;

/* ---- 日志 ------------------------------------------------- */

void gfx_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sc_gfx] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ---- 句柄池 ----------------------------------------------- */

void gfx_pool_init(gfx_pool* p, int num) {
    p->size = num + 1;                       /* 槽 0 保留 */
    p->queue_top = 0;
    p->free_queue = (int*)calloc((size_t)num, sizeof(int));
    p->gen = (uint32_t*)calloc((size_t)p->size, sizeof(uint32_t));
    for (int i = num; i >= 1; i--)           /* 低索引先出 */
        p->free_queue[p->queue_top++] = i;
}

void gfx_pool_free(gfx_pool* p) {
    free(p->free_queue); p->free_queue = NULL;
    free(p->gen);        p->gen = NULL;
    p->size = 0; p->queue_top = 0;
}

uint32_t gfx_pool_alloc(gfx_pool* p) {
    if (p->queue_top <= 0) return 0;
    int index = p->free_queue[--p->queue_top];
    uint32_t gen = ++p->gen[index];
    if ((gen & 0xFFFF) == 0) gen = ++p->gen[index];   /* 代数跳过 0 */
    return ((gen & 0xFFFF) << 16) | (uint32_t)index;
}

void gfx_pool_release(gfx_pool* p, uint32_t id) {
    int index = gfx_slot_index(id);
    if (index <= 0 || index >= p->size) return;
    p->free_queue[p->queue_top++] = index;
}

/* ---- 查表 ------------------------------------------------- */

#define LOOKUP_IMPL(name, poolfield, arrfield, type)                          \
    type* gfx_lookup_##name(uint32_t id) {                                \
        if (!id) return NULL;                                                 \
        int index = gfx_slot_index(id);                                   \
        if (index <= 0 || index >= g_gfx.poolfield.size) return NULL;       \
        type* res = &g_gfx.arrfield[index];                                 \
        if (res->slot.id != id) return NULL;                                  \
        return res;                                                           \
    }

LOOKUP_IMPL(buffer,   buffer_pool,   buffers,   gfx_buffer_t)
LOOKUP_IMPL(image,    image_pool,    images,    gfx_image_t)
LOOKUP_IMPL(sampler,  sampler_pool,  samplers,  gfx_sampler_t)
LOOKUP_IMPL(shader,   shader_pool,   shaders,   gfx_shader_t)
LOOKUP_IMPL(pipeline, pipeline_pool, pipelines, gfx_pipeline_t)

/* ---- 缺省值 ----------------------------------------------- */

#define DEF(v, d) ((v) == 0 ? (d) : (v))

static void resolveBufferDesc(sc_gfx_buffer_desc* d) {
    d->kind  = DEF(d->kind, SC_GFX_BUFFERKIND_VERTEX);
    d->usage = DEF(d->usage, SC_GFX_USAGE_IMMUTABLE);
    if (d->size == 0) d->size = d->data.size;
}

static void resolveImageDesc(sc_gfx_image_desc* d) {
    d->kind      = DEF(d->kind, SC_GFX_IMAGEKIND_2D);
    d->slices    = DEF(d->slices, 1);
    d->mip_count = DEF(d->mip_count, 1);
    d->format    = DEF(d->format, SC_GPU_PIXELFORMAT_RGBA8);
    d->usage     = DEF(d->usage, SC_GFX_USAGE_IMMUTABLE);
    d->sample_count = DEF(d->sample_count, 1);
}

static void resolveSamplerDesc(sc_gfx_sampler_desc* d) {
    d->min_filter    = DEF(d->min_filter, SC_GFX_FILTER_NEAREST);
    d->mag_filter    = DEF(d->mag_filter, SC_GFX_FILTER_NEAREST);
    d->mipmap_filter = DEF(d->mipmap_filter, SC_GFX_FILTER_NEAREST);
    d->wrap_u = DEF(d->wrap_u, SC_GFX_WRAP_REPEAT);
    d->wrap_v = DEF(d->wrap_v, SC_GFX_WRAP_REPEAT);
    d->wrap_w = DEF(d->wrap_w, SC_GFX_WRAP_REPEAT);
    d->compare = DEF(d->compare, SC_GFX_COMPARE_ALWAYS);
}

/* 顶点格式字节宽 */
static int vertexFormatSize(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:  return 4;
        case SC_GFX_VERTEXFORMAT_FLOAT2: return 8;
        case SC_GFX_VERTEXFORMAT_FLOAT3: return 12;
        case SC_GFX_VERTEXFORMAT_FLOAT4: return 16;
        case SC_GFX_VERTEXFORMAT_BYTE4: case SC_GFX_VERTEXFORMAT_BYTE4N:
        case SC_GFX_VERTEXFORMAT_UBYTE4: case SC_GFX_VERTEXFORMAT_UBYTE4N:
        case SC_GFX_VERTEXFORMAT_SHORT2: case SC_GFX_VERTEXFORMAT_SHORT2N:
        case SC_GFX_VERTEXFORMAT_USHORT2: case SC_GFX_VERTEXFORMAT_USHORT2N:
        case SC_GFX_VERTEXFORMAT_HALF2: case SC_GFX_VERTEXFORMAT_UINT10N2:
        case SC_GFX_VERTEXFORMAT_UINT:   return 4;
        case SC_GFX_VERTEXFORMAT_SHORT4: case SC_GFX_VERTEXFORMAT_SHORT4N:
        case SC_GFX_VERTEXFORMAT_USHORT4: case SC_GFX_VERTEXFORMAT_USHORT4N:
        case SC_GFX_VERTEXFORMAT_HALF4: return 8;
        default: return 0;
    }
}

/* 交换链缺省参照：gpu 当前 surface（无则兜底常量） */
static void swapchainDefaults(sc_gpu_surface_desc* sd) {
    if (sc_gpu_query_surface_info(0, sd)) return;
    memset(sd, 0, sizeof(*sd));
    sd->color_format = SC_GPU_PIXELFORMAT_BGRA8;
    sd->depth_format = SC_GPU_PIXELFORMAT_DEPTH_STENCIL;
    sd->sample_count = 1;
}

static void resolvePipelineDesc(sc_gfx_pipeline_desc* d) {
    sc_gpu_surface_desc sd;
    swapchainDefaults(&sd);
    d->primitive  = DEF(d->primitive, SC_GFX_PRIMITIVE_TRIANGLES);
    d->index_type = DEF(d->index_type, SC_GFX_INDEXTYPE_NONE);
    d->cull       = DEF(d->cull, SC_GFX_CULL_NONE);
    d->winding    = DEF(d->winding, SC_GFX_WINDING_CCW);
    d->color_count = DEF(d->color_count, 1);
    d->sample_count = DEF(d->sample_count, sd.sample_count);
    d->depth.compare = DEF(d->depth.compare, SC_GFX_COMPARE_ALWAYS);
    if (d->depth.format == 0) d->depth.format = sd.depth_format;
    if (d->stencil.enabled) {
        sc_gfx_stencil_face_state* faces[2] = { &d->stencil.front, &d->stencil.back };
        for (int i = 0; i < 2; i++) {
            faces[i]->compare       = DEF(faces[i]->compare, SC_GFX_COMPARE_ALWAYS);
            faces[i]->fail_op       = DEF(faces[i]->fail_op, SC_GFX_STENCILOP_KEEP);
            faces[i]->depth_fail_op = DEF(faces[i]->depth_fail_op, SC_GFX_STENCILOP_KEEP);
            faces[i]->pass_op       = DEF(faces[i]->pass_op, SC_GFX_STENCILOP_KEEP);
        }
        d->stencil.read_mask  = DEF(d->stencil.read_mask, 0xFF);
        d->stencil.write_mask = DEF(d->stencil.write_mask, 0xFF);
    }
    for (int i = 0; i < d->color_count; i++) {
        sc_gfx_color_target_state* c = &d->colors[i];
        c->format = DEF(c->format, sd.color_format);
        if (c->blend.enabled) {
            c->blend.src_factor_rgb   = DEF(c->blend.src_factor_rgb, SC_GFX_BLEND_ONE);
            c->blend.dst_factor_rgb   = DEF(c->blend.dst_factor_rgb, SC_GFX_BLEND_ZERO);
            c->blend.op_rgb           = DEF(c->blend.op_rgb, SC_GFX_BLENDOP_ADD);
            c->blend.src_factor_alpha = DEF(c->blend.src_factor_alpha, c->blend.src_factor_rgb);
            c->blend.dst_factor_alpha = DEF(c->blend.dst_factor_alpha, c->blend.dst_factor_rgb);
            c->blend.op_alpha         = DEF(c->blend.op_alpha, c->blend.op_rgb);
        }
    }
    /* 顶点布局：自动 stride / offset（同 sokol：offset 0 按声明序累加） */
    int auto_offset[SC_GFX_MAX_VERTEX_BUFFERS] = {0};
    bool use_auto = true;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++)
        if (d->attrs[i].format != SC_GFX_VERTEXFORMAT_INVALID && d->attrs[i].offset != 0)
            use_auto = false;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++) {
        sc_gfx_vertex_attr* a = &d->attrs[i];
        if (a->format == SC_GFX_VERTEXFORMAT_INVALID) continue;
        if (use_auto) {
            a->offset = auto_offset[a->buffer_index];
            auto_offset[a->buffer_index] += vertexFormatSize(a->format);
        }
    }
    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++)
        if (d->buffers[i].stride == 0)
            d->buffers[i].stride = auto_offset[i];
}

static void resolvePassAction(sc_gfx_pass_action* a, int color_count,
                              const bool has_resolve[SC_GFX_MAX_COLOR_ATTACHMENTS]) {
    for (int i = 0; i < color_count; i++) {
        a->colors[i].load  = DEF(a->colors[i].load, SC_GFX_LOADACTION_CLEAR);
        a->colors[i].store = DEF(a->colors[i].store,
            (has_resolve && has_resolve[i]) ? SC_GFX_STOREACTION_RESOLVE
                                            : SC_GFX_STOREACTION_STORE);
    }
    a->depth.load  = DEF(a->depth.load, SC_GFX_LOADACTION_CLEAR);
    a->depth.store = DEF(a->depth.store, SC_GFX_STOREACTION_DONTCARE);
    if (a->depth.clear_depth == 0.0f) a->depth.clear_depth = 1.0f;
}

/* ---- 后端选择（跟随 gpu） ----------------------------------- */

static const gfx_backend_api* pickBackend(void) {
    switch ((sc_gpu_backend)sc_gpu_query_backend()) {
        case SC_GPU_BACKEND_METAL:
#ifdef SC_GPU_METAL
            return gfx_backend_metal();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_GL:
#ifdef SC_GPU_GL
            return gfx_backend_gl();
#else
            return NULL;
#endif
        case SC_GPU_BACKEND_NULL:
        default:
            return gfx_backend_null();
    }
}

/* ---- 生命周期 --------------------------------------------- */

int sc_gfx_init(const sc_gfx_desc* desc) {
    if (g_gfx.valid) { gfx_log("init: 已初始化"); return 0; }
    if (!sc_gpu_isvalid()) { gfx_log("init: 须先 sc_gpu_init"); return 0; }
    memset(&g_gfx, 0, sizeof(g_gfx));
    g_gfx.desc = desc ? *desc : (sc_gfx_desc){0};
    g_gfx.desc.buffer_pool_size   = DEF(g_gfx.desc.buffer_pool_size, 128);
    g_gfx.desc.image_pool_size    = DEF(g_gfx.desc.image_pool_size, 128);
    g_gfx.desc.sampler_pool_size  = DEF(g_gfx.desc.sampler_pool_size, 64);
    g_gfx.desc.shader_pool_size   = DEF(g_gfx.desc.shader_pool_size, 64);
    g_gfx.desc.pipeline_pool_size = DEF(g_gfx.desc.pipeline_pool_size, 64);

    const gfx_backend_api* api = pickBackend();
    if (!api) { gfx_log("init: gpu 后端对应的 gfx 翻译未编入本库"); return 0; }

    gfx_pool_init(&g_gfx.buffer_pool,   g_gfx.desc.buffer_pool_size);
    gfx_pool_init(&g_gfx.image_pool,    g_gfx.desc.image_pool_size);
    gfx_pool_init(&g_gfx.sampler_pool,  g_gfx.desc.sampler_pool_size);
    gfx_pool_init(&g_gfx.shader_pool,   g_gfx.desc.shader_pool_size);
    gfx_pool_init(&g_gfx.pipeline_pool, g_gfx.desc.pipeline_pool_size);
    g_gfx.buffers   = (gfx_buffer_t*)  calloc((size_t)g_gfx.buffer_pool.size,   sizeof(gfx_buffer_t));
    g_gfx.images    = (gfx_image_t*)   calloc((size_t)g_gfx.image_pool.size,    sizeof(gfx_image_t));
    g_gfx.samplers  = (gfx_sampler_t*) calloc((size_t)g_gfx.sampler_pool.size,  sizeof(gfx_sampler_t));
    g_gfx.shaders   = (gfx_shader_t*)  calloc((size_t)g_gfx.shader_pool.size,   sizeof(gfx_shader_t));
    g_gfx.pipelines = (gfx_pipeline_t*)calloc((size_t)g_gfx.pipeline_pool.size, sizeof(gfx_pipeline_t));

    if (!api->init(&g_gfx.desc)) {
        gfx_log("init: 后端 %s 初始化失败", api->name);
        sc_gfx_shutdown();
        return 0;
    }
    g_gfx.api = api;
    g_gfx.valid = true;
    return 1;
}

void sc_gfx_shutdown(void) {
    if (g_gfx.api) {
        /* 逐资源销毁（后端仍活着时） */
        for (int i = 1; i < g_gfx.pipeline_pool.size; i++)
            if (g_gfx.pipelines[i].slot.state == _SC_GFX_SLOT_VALID)
                g_gfx.api->pipeline_destroy(&g_gfx.pipelines[i]);
        for (int i = 1; i < g_gfx.shader_pool.size; i++)
            if (g_gfx.shaders[i].slot.state == _SC_GFX_SLOT_VALID)
                g_gfx.api->shader_destroy(&g_gfx.shaders[i]);
        for (int i = 1; i < g_gfx.sampler_pool.size; i++)
            if (g_gfx.samplers[i].slot.state == _SC_GFX_SLOT_VALID)
                g_gfx.api->sampler_destroy(&g_gfx.samplers[i]);
        for (int i = 1; i < g_gfx.image_pool.size; i++)
            if (g_gfx.images[i].slot.state == _SC_GFX_SLOT_VALID)
                g_gfx.api->image_destroy(&g_gfx.images[i]);
        for (int i = 1; i < g_gfx.buffer_pool.size; i++)
            if (g_gfx.buffers[i].slot.state == _SC_GFX_SLOT_VALID)
                g_gfx.api->buffer_destroy(&g_gfx.buffers[i]);
        g_gfx.api->shutdown();
    }
    free(g_gfx.buffers);   free(g_gfx.images); free(g_gfx.samplers);
    free(g_gfx.shaders);   free(g_gfx.pipelines);
    gfx_pool_free(&g_gfx.buffer_pool);
    gfx_pool_free(&g_gfx.image_pool);
    gfx_pool_free(&g_gfx.sampler_pool);
    gfx_pool_free(&g_gfx.shader_pool);
    gfx_pool_free(&g_gfx.pipeline_pool);
    memset(&g_gfx, 0, sizeof(g_gfx));
}

int sc_gfx_isvalid(void) { return g_gfx.valid ? 1 : 0; }

void sc_gfx_finish(void) {
    if (g_gfx.valid && g_gfx.api->finish) g_gfx.api->finish();
}

/* ---- 能力查询 ----------------------------------------------- */

sc_gfx_pixelformat_info sc_gfx_query_pixelformat(sc_gpu_pixel_format fmt) {
    sc_gfx_pixelformat_info info;
    memset(&info, 0, sizeof(info));
    if (g_gfx.valid && g_gfx.api->query_pixelformat)
        g_gfx.api->query_pixelformat(fmt, &info);
    return info;
}

/* ---- 资源创建/销毁 ----------------------------------------- */

sc_gfx_buffer sc_gfx_make_buffer(const sc_gfx_buffer_desc* desc) {
    if (!g_gfx.valid || !desc) return 0;
    uint32_t id = gfx_pool_alloc(&g_gfx.buffer_pool);
    if (!id) { gfx_log("make_buffer: 池满"); return 0; }
    gfx_buffer_t* buf = &g_gfx.buffers[gfx_slot_index(id)];
    memset(buf, 0, sizeof(*buf));
    buf->slot.id = id;
    buf->desc = *desc;
    resolveBufferDesc(&buf->desc);
    if (buf->desc.usage == SC_GFX_USAGE_IMMUTABLE && !buf->desc.data.ptr) {
        gfx_log("make_buffer: IMMUTABLE 须提供初始数据");
        buf->slot.state = _SC_GFX_SLOT_FAILED;
    } else {
        buf->slot.state = g_gfx.api->buffer_create(buf) ? _SC_GFX_SLOT_VALID
                                                          : _SC_GFX_SLOT_FAILED;
    }
    return id;
}

sc_gfx_image sc_gfx_make_image(const sc_gfx_image_desc* desc) {
    if (!g_gfx.valid || !desc) return 0;
    if (desc->memimg && (desc->kind > SC_GFX_IMAGEKIND_2D || desc->mip_count > 1)) {
        gfx_log("make_image: memimg 绑定限 2D 单 mip");
        return 0;
    }
    uint32_t id = gfx_pool_alloc(&g_gfx.image_pool);
    if (!id) { gfx_log("make_image: 池满"); return 0; }
    gfx_image_t* img = &g_gfx.images[gfx_slot_index(id)];
    memset(img, 0, sizeof(*img));
    img->slot.id = id;
    img->desc = *desc;
    resolveImageDesc(&img->desc);
    img->slot.state = g_gfx.api->image_create(img) ? _SC_GFX_SLOT_VALID
                                                     : _SC_GFX_SLOT_FAILED;
    return id;
}

sc_gfx_sampler sc_gfx_make_sampler(const sc_gfx_sampler_desc* desc) {
    if (!g_gfx.valid || !desc) return 0;
    uint32_t id = gfx_pool_alloc(&g_gfx.sampler_pool);
    if (!id) { gfx_log("make_sampler: 池满"); return 0; }
    gfx_sampler_t* smp = &g_gfx.samplers[gfx_slot_index(id)];
    memset(smp, 0, sizeof(*smp));
    smp->slot.id = id;
    smp->desc = *desc;
    resolveSamplerDesc(&smp->desc);
    smp->slot.state = g_gfx.api->sampler_create(smp) ? _SC_GFX_SLOT_VALID
                                                       : _SC_GFX_SLOT_FAILED;
    return id;
}

sc_gfx_shader sc_gfx_make_shader(const sc_gfx_shader_desc* desc) {
    if (!g_gfx.valid || !desc) return 0;
    uint32_t id = gfx_pool_alloc(&g_gfx.shader_pool);
    if (!id) { gfx_log("make_shader: 池满"); return 0; }
    gfx_shader_t* shd = &g_gfx.shaders[gfx_slot_index(id)];
    memset(shd, 0, sizeof(*shd));
    shd->slot.id = id;
    shd->has_cs = desc->cs.code.ptr != NULL;

    /* 反射清单优先；无清单时收手工绑定描述 */
    if (desc->reflect_json) {
        if (!gfx_parse_reflect(desc->reflect_json, &shd->reflect)) {
            gfx_log("make_shader: 反射清单解析失败");
            shd->slot.state = _SC_GFX_SLOT_FAILED;
            return id;
        }
    } else {
        gfx_reflect* r = &shd->reflect;
        for (int i = 0; i < SC_GFX_MAX_UNIFORM_BLOCKS * 2; i++) {
            const sc_gfx_uniform_block_desc* ub = &desc->uniform_blocks[i];
            if (ub->size == 0) continue;
            gfx_reflect_block* b = &r->blocks[r->block_count++];
            b->stage = ub->stage; b->slot = ub->slot; b->size = ub->size;
            if (ub->name) { strncpy(b->name, ub->name, sizeof(b->name) - 1); }
        }
        for (int i = 0; i < SC_GFX_MAX_IMAGES; i++) {
            const sc_gfx_image_slot_desc* is = &desc->images[i];
            if (!is->name) continue;
            gfx_reflect_sampler* s = &r->samplers[r->sampler_count++];
            s->stage = is->stage; s->slot = is->slot;
            strncpy(s->name, is->name, sizeof(s->name) - 1);
        }
    }
    shd->slot.state = g_gfx.api->shader_create(shd, desc) ? _SC_GFX_SLOT_VALID
                                                            : _SC_GFX_SLOT_FAILED;
    return id;
}

sc_gfx_pipeline sc_gfx_make_pipeline(const sc_gfx_pipeline_desc* desc) {
    if (!g_gfx.valid || !desc) return 0;
    gfx_shader_t* shd = gfx_lookup_shader(desc->shader);
    if (!shd || shd->slot.state != _SC_GFX_SLOT_VALID) {
        gfx_log("make_pipeline: 无效 shader");
        return 0;
    }
    uint32_t id = gfx_pool_alloc(&g_gfx.pipeline_pool);
    if (!id) { gfx_log("make_pipeline: 池满"); return 0; }
    gfx_pipeline_t* pip = &g_gfx.pipelines[gfx_slot_index(id)];
    memset(pip, 0, sizeof(*pip));
    pip->slot.id = id;
    pip->desc = *desc;
    pip->shader = shd;
    resolvePipelineDesc(&pip->desc);
    pip->slot.state = g_gfx.api->pipeline_create(pip) ? _SC_GFX_SLOT_VALID
                                                        : _SC_GFX_SLOT_FAILED;
    return id;
}

#define DESTROY_IMPL(name, poolfield, type, apifn)                             \
    void sc_gfx_destroy_##name(type hnd) {                                     \
        if (!g_gfx.valid) return;                                            \
        gfx_##name##_t* res = gfx_lookup_##name(hnd);                  \
        if (!res) return;                                                      \
        if (res->slot.state == _SC_GFX_SLOT_VALID) g_gfx.api->apifn(res);    \
        res->slot.id = 0;                                                      \
        res->slot.state = _SC_GFX_SLOT_FREE;                                   \
        gfx_pool_release(&g_gfx.poolfield, hnd);                         \
    }

DESTROY_IMPL(buffer,   buffer_pool,   sc_gfx_buffer,   buffer_destroy)
DESTROY_IMPL(image,    image_pool,    sc_gfx_image,    image_destroy)
DESTROY_IMPL(sampler,  sampler_pool,  sc_gfx_sampler,  sampler_destroy)
DESTROY_IMPL(shader,   shader_pool,   sc_gfx_shader,   shader_destroy)
DESTROY_IMPL(pipeline, pipeline_pool, sc_gfx_pipeline, pipeline_destroy)

/* ---- 资源更新 --------------------------------------------- */

void sc_gfx_update_buffer(sc_gfx_buffer hnd, const sc_gfx_range* data) {
    if (!g_gfx.valid || !data || !data->ptr) return;
    gfx_buffer_t* buf = gfx_lookup_buffer(hnd);
    if (!buf || buf->slot.state != _SC_GFX_SLOT_VALID) return;
    if (buf->desc.usage == SC_GFX_USAGE_IMMUTABLE) {
        gfx_log("update_buffer: IMMUTABLE 不可更新"); return;
    }
    if (data->size > buf->desc.size) {
        gfx_log("update_buffer: 数据超出缓冲大小"); return;
    }
    g_gfx.api->buffer_update(buf, data, 0);
}

int sc_gfx_append_buffer(sc_gfx_buffer hnd, const sc_gfx_range* data) {
    if (!g_gfx.valid || !data || !data->ptr) return 0;
    gfx_buffer_t* buf = gfx_lookup_buffer(hnd);
    if (!buf || buf->slot.state != _SC_GFX_SLOT_VALID) return 0;
    /* 按 4 字节对齐追加 */
    int offset = (buf->append_pos + 3) & ~3;
    if ((size_t)offset + data->size > buf->desc.size) {
        buf->append_overflow = true;
        gfx_log("append_buffer: 溢出");
        return offset;
    }
    g_gfx.api->buffer_update(buf, data, offset);
    buf->append_pos = offset + (int)data->size;
    return offset;
}

void sc_gfx_update_image(sc_gfx_image hnd, const sc_gfx_image_data* data) {
    if (!g_gfx.valid || !data) return;
    gfx_image_t* img = gfx_lookup_image(hnd);
    if (!img || img->slot.state != _SC_GFX_SLOT_VALID) return;
    if (img->desc.usage == SC_GFX_USAGE_IMMUTABLE) {
        gfx_log("update_image: IMMUTABLE 不可更新"); return;
    }
    g_gfx.api->image_update(img, data);
}

/* ---- 状态查询 --------------------------------------------- */

static int slotToState(gfx_slot_state s) {
    switch (s) {
        case _SC_GFX_SLOT_VALID:  return SC_GFX_RESOURCESTATE_VALID;
        case _SC_GFX_SLOT_FAILED: return SC_GFX_RESOURCESTATE_FAILED;
        case _SC_GFX_SLOT_ALLOC:  return SC_GFX_RESOURCESTATE_INITIAL;
        default:                  return SC_GFX_RESOURCESTATE_INVALID;
    }
}

int sc_gfx_query_buffer_state(sc_gfx_buffer h) {
    gfx_buffer_t* r = gfx_lookup_buffer(h);
    return r ? slotToState(r->slot.state) : SC_GFX_RESOURCESTATE_INVALID;
}
int sc_gfx_query_image_state(sc_gfx_image h) {
    gfx_image_t* r = gfx_lookup_image(h);
    return r ? slotToState(r->slot.state) : SC_GFX_RESOURCESTATE_INVALID;
}
int sc_gfx_query_shader_state(sc_gfx_shader h) {
    gfx_shader_t* r = gfx_lookup_shader(h);
    return r ? slotToState(r->slot.state) : SC_GFX_RESOURCESTATE_INVALID;
}
int sc_gfx_query_pipeline_state(sc_gfx_pipeline h) {
    gfx_pipeline_t* r = gfx_lookup_pipeline(h);
    return r ? slotToState(r->slot.state) : SC_GFX_RESOURCESTATE_INVALID;
}

/* ---- 帧 --------------------------------------------------- */

void sc_gfx_begin_pass(const sc_gfx_pass* pass) {
    if (!g_gfx.valid || !pass) return;
    if (g_gfx.in_pass) { gfx_log("begin_pass: pass 嵌套"); return; }

    sc_gfx_pass p = *pass;
    gfx_image_t* colors[SC_GFX_MAX_COLOR_ATTACHMENTS] = {0};
    gfx_image_t* resolves[SC_GFX_MAX_COLOR_ATTACHMENTS] = {0};
    bool has_resolve[SC_GFX_MAX_COLOR_ATTACHMENTS] = {0};
    gfx_image_t* depth = NULL;
    int color_count = 0;
    for (int i = 0; i < SC_GFX_MAX_COLOR_ATTACHMENTS; i++) {
        if (!p.colors[i].image) break;
        colors[i] = gfx_lookup_image(p.colors[i].image);
        if (!colors[i] || colors[i]->slot.state != _SC_GFX_SLOT_VALID) {
            gfx_log("begin_pass: 无效颜色附件 %d", i); return;
        }
        if (p.resolves[i].image) {
            resolves[i] = gfx_lookup_image(p.resolves[i].image);
            if (!resolves[i] || resolves[i]->slot.state != _SC_GFX_SLOT_VALID) {
                gfx_log("begin_pass: 无效 resolve 附件 %d", i); return;
            }
            has_resolve[i] = true;
        }
        color_count++;
    }
    if (p.depth_stencil.image) {
        depth = gfx_lookup_image(p.depth_stencil.image);
        if (!depth || depth->slot.state != _SC_GFX_SLOT_VALID) {
            gfx_log("begin_pass: 无效深度附件"); return;
        }
    }
    g_gfx.pass_is_swapchain = (color_count == 0 && !p.compute);
    if (g_gfx.pass_is_swapchain && !sc_gpu_query_current_surface()) {
        gfx_log("begin_pass: gpu 无当前 surface（先 sc_gpu_make_current）"); return;
    }
    /* 交换链 MSAA：后端自行解析到 drawable，store 默认 RESOLVE */
    bool sc_resolve[SC_GFX_MAX_COLOR_ATTACHMENTS] = {0};
    if (g_gfx.pass_is_swapchain) {
        sc_gpu_surface_desc sd;
        if (sc_gpu_query_surface_info(0, &sd) && sd.sample_count > 1)
            sc_resolve[0] = true;
    }
    resolvePassAction(&p.action, g_gfx.pass_is_swapchain ? 1 : color_count,
                      g_gfx.pass_is_swapchain ? sc_resolve : has_resolve);
    g_gfx.in_pass = true;
    g_gfx.cur_pipeline = NULL;
    g_gfx.api->begin_pass(&p, colors, color_count, resolves, depth);
}

void sc_gfx_apply_viewport(int x, int y, int w, int h, int origin_top_left) {
    if (!g_gfx.valid || !g_gfx.in_pass) return;
    g_gfx.api->apply_viewport(x, y, w, h, origin_top_left != 0);
}

void sc_gfx_apply_scissor(int x, int y, int w, int h, int origin_top_left) {
    if (!g_gfx.valid || !g_gfx.in_pass) return;
    g_gfx.api->apply_scissor(x, y, w, h, origin_top_left != 0);
}

void sc_gfx_apply_pipeline(sc_gfx_pipeline hnd) {
    if (!g_gfx.valid || !g_gfx.in_pass) return;
    gfx_pipeline_t* pip = gfx_lookup_pipeline(hnd);
    if (!pip || pip->slot.state != _SC_GFX_SLOT_VALID) {
        gfx_log("apply_pipeline: 无效管线"); return;
    }
    g_gfx.cur_pipeline = pip;
    g_gfx.api->apply_pipeline(pip);
}

void sc_gfx_apply_bindings(const sc_gfx_bindings* bnd) {
    if (!g_gfx.valid || !g_gfx.in_pass || !bnd) return;
    if (!g_gfx.cur_pipeline) { gfx_log("apply_bindings: 未 apply_pipeline"); return; }

    gfx_buffer_t* vbufs[SC_GFX_MAX_VERTEX_BUFFERS] = {0};
    gfx_buffer_t* ibuf = NULL;
    /* [0]=vs [1]=fs [2]=cs */
    gfx_image_t*   imgs[3][SC_GFX_MAX_IMAGES] = {{0}};
    gfx_sampler_t* smps[3][SC_GFX_MAX_SAMPLERS] = {{0}};
    gfx_buffer_t*  sbufs[3][SC_GFX_MAX_STORAGE_BUFFERS] = {{0}};

    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++) {
        if (!bnd->vertex_buffers[i]) continue;
        vbufs[i] = gfx_lookup_buffer(bnd->vertex_buffers[i]);
        if (!vbufs[i]) { gfx_log("apply_bindings: 无效顶点缓冲 %d", i); return; }
    }
    if (bnd->index_buffer) {
        ibuf = gfx_lookup_buffer(bnd->index_buffer);
        if (!ibuf) { gfx_log("apply_bindings: 无效索引缓冲"); return; }
    }
    const sc_gfx_stage_bindings* stages[3] = { &bnd->vs, &bnd->fs, &bnd->cs };
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < SC_GFX_MAX_IMAGES; i++)
            if (stages[s]->images[i])
                imgs[s][i] = gfx_lookup_image(stages[s]->images[i]);
        for (int i = 0; i < SC_GFX_MAX_SAMPLERS; i++)
            if (stages[s]->samplers[i])
                smps[s][i] = gfx_lookup_sampler(stages[s]->samplers[i]);
        for (int i = 0; i < SC_GFX_MAX_STORAGE_BUFFERS; i++)
            if (stages[s]->storage_buffers[i])
                sbufs[s][i] = gfx_lookup_buffer(stages[s]->storage_buffers[i]);
    }
    g_gfx.api->apply_bindings(g_gfx.cur_pipeline, bnd, vbufs, ibuf, imgs, smps, sbufs);
}

void sc_gfx_apply_uniforms(int stage, int slot, const void* data, uint64_t size) {
    if (!g_gfx.valid || !g_gfx.in_pass || !data || !size) return;
    g_gfx.api->apply_uniforms(stage, slot, data, (size_t)size);
}

void sc_gfx_draw(int base_element, int element_count, int instance_count) {
    if (!g_gfx.valid || !g_gfx.in_pass || !g_gfx.cur_pipeline) return;
    if (instance_count <= 0) instance_count = 1;
    g_gfx.api->draw(base_element, element_count, instance_count);
}

void sc_gfx_dispatch(int gx, int gy, int gz) {
    if (!g_gfx.valid || !g_gfx.in_pass || !g_gfx.cur_pipeline) return;
    if (!g_gfx.cur_pipeline->desc.compute) {
        gfx_log("dispatch: 当前非计算管线"); return;
    }
    g_gfx.api->dispatch(gx, gy, gz);
}

void sc_gfx_end_pass(void) {
    if (!g_gfx.valid || !g_gfx.in_pass) return;
    g_gfx.api->end_pass();
    g_gfx.in_pass = false;
    g_gfx.cur_pipeline = NULL;
}

void sc_gfx_commit(void) {
    if (!g_gfx.valid) return;
    g_gfx.api->commit();   /* 后端末尾调 sc_gpu_frame_end() */
    g_gfx.frame_index++;
    /* 每帧重置 append 位置（stream 语义） */
    for (int i = 1; i < g_gfx.buffer_pool.size; i++) {
        gfx_buffer_t* b = &g_gfx.buffers[i];
        if (b->slot.state == _SC_GFX_SLOT_VALID && b->desc.usage == SC_GFX_USAGE_STREAM) {
            b->append_pos = 0;
            b->append_overflow = false;
        }
    }
}
