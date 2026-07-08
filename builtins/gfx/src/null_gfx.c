/* ============================================================
 * null_gfx.c —— 空渲染后端：全部操作成功但不触达任何硬件
 * ============================================================
 * 用途：无 GPU 环境（CI/服务器）跑通资源生命周期与帧循环逻辑；
 *       亦作为新后端移植的最小参照。
 * ============================================================ */

#include "internal.h"

static bool nullInit(const sc_gfx_desc* desc) { (void)desc; return true; }
static void nullShutdown(void) {}
static void nullFinish(void) {}

static bool nullBufferCreate(gfx_buffer_t* buf) { (void)buf; return true; }
static void nullBufferDestroy(gfx_buffer_t* buf) { (void)buf; }
static void nullBufferUpdate(gfx_buffer_t* buf, const sc_gfx_range* d, int off) {
    (void)buf; (void)d; (void)off;
}

static bool nullImageCreate(gfx_image_t* img) { (void)img; return true; }
static void nullImageDestroy(gfx_image_t* img) { (void)img; }
static void nullImageUpdate(gfx_image_t* img, const sc_gfx_image_data* d) {
    (void)img; (void)d;
}

static bool nullSamplerCreate(gfx_sampler_t* smp) { (void)smp; return true; }
static void nullSamplerDestroy(gfx_sampler_t* smp) { (void)smp; }

static bool nullShaderCreate(gfx_shader_t* shd, const sc_gfx_shader_desc* d) {
    (void)shd; (void)d; return true;
}
static void nullShaderDestroy(gfx_shader_t* shd) { (void)shd; }

static bool nullPipelineCreate(gfx_pipeline_t* pip) { (void)pip; return true; }
static void nullPipelineDestroy(gfx_pipeline_t* pip) { (void)pip; }

static void nullBeginPass(const sc_gfx_pass* pass, gfx_image_t* colors[],
                          int n, gfx_image_t* resolves[], gfx_image_t* depth) {
    (void)pass; (void)colors; (void)n; (void)resolves; (void)depth;
    if (n == 0 && !pass->compute) {
        sc_gpu_frame f;
        sc_gpu_frame_acquire(&f);   /* 走完帧交付协议 */
    }
}
static void nullApplyViewport(int x, int y, int w, int h, bool tl) {
    (void)x; (void)y; (void)w; (void)h; (void)tl;
}
static void nullApplyScissor(int x, int y, int w, int h, bool tl) {
    (void)x; (void)y; (void)w; (void)h; (void)tl;
}
static void nullApplyPipeline(gfx_pipeline_t* pip) { (void)pip; }
static void nullApplyBindings(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                              gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                              gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                              gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                              gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]) {
    (void)pip; (void)bnd; (void)vbufs; (void)ibuf; (void)imgs; (void)smps; (void)sbufs;
}
static void nullApplyUniforms(int stage, int slot, const void* d, size_t sz) {
    (void)stage; (void)slot; (void)d; (void)sz;
}
static void nullDraw(int base, int count, int inst) { (void)base; (void)count; (void)inst; }
static void nullDispatch(int gx, int gy, int gz) { (void)gx; (void)gy; (void)gz; }
static void nullEndPass(void) {}
static void nullCommit(void) { sc_gpu_frame_end(); }
static void nullQueryPixelformat(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out) {
    (void)fmt;
    out->sample = 1; out->filter = 1; out->render = 1;
    out->blend = 1; out->msaa = 1;
    out->depth = (fmt == SC_GPU_PIXELFORMAT_DEPTH || fmt == SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
}

static const gfx_backend_api nullApi = {
    .name = "null",
    .init = nullInit,
    .shutdown = nullShutdown,
    .finish = nullFinish,
    .buffer_create = nullBufferCreate,
    .buffer_destroy = nullBufferDestroy,
    .buffer_update = nullBufferUpdate,
    .image_create = nullImageCreate,
    .image_destroy = nullImageDestroy,
    .image_update = nullImageUpdate,
    .sampler_create = nullSamplerCreate,
    .sampler_destroy = nullSamplerDestroy,
    .shader_create = nullShaderCreate,
    .shader_destroy = nullShaderDestroy,
    .pipeline_create = nullPipelineCreate,
    .pipeline_destroy = nullPipelineDestroy,
    .begin_pass = nullBeginPass,
    .apply_viewport = nullApplyViewport,
    .apply_scissor = nullApplyScissor,
    .apply_pipeline = nullApplyPipeline,
    .apply_bindings = nullApplyBindings,
    .apply_uniforms = nullApplyUniforms,
    .draw = nullDraw,
    .dispatch = nullDispatch,
    .end_pass = nullEndPass,
    .commit = nullCommit,
    .query_pixelformat = nullQueryPixelformat,
};

const gfx_backend_api* gfx_backend_null(void) { return &nullApi; }
