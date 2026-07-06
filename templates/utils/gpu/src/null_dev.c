/* ============================================================
 * null_dev.c —— 空后端：全部操作成功但不触达任何硬件
 * ============================================================
 * 用途：无 GPU 环境（CI/服务器）跑通资源生命周期与帧循环逻辑；
 *       亦作为新后端移植的最小参照。
 * ============================================================ */

#include "internal.h"

static bool nullInit(const sc_gpu_desc* desc) { (void)desc; return true; }
static void nullShutdown(void) {}

static bool nullSurfaceCreate(_sc_gpu_surface_t* surf) { (void)surf; return true; }
static void nullSurfaceDestroy(_sc_gpu_surface_t* surf) { (void)surf; }
static void nullSurfaceActivate(_sc_gpu_surface_t* surf) { (void)surf; }
static void nullSurfaceResize(_sc_gpu_surface_t* surf, int w, int h) {
    (void)surf; (void)w; (void)h;
}

static bool nullBufferCreate(_sc_gpu_buffer_t* buf) { (void)buf; return true; }
static void nullBufferDestroy(_sc_gpu_buffer_t* buf) { (void)buf; }
static void nullBufferUpdate(_sc_gpu_buffer_t* buf, const sc_gpu_range* d, int off) {
    (void)buf; (void)d; (void)off;
}

static bool nullImageCreate(_sc_gpu_image_t* img) { (void)img; return true; }
static void nullImageDestroy(_sc_gpu_image_t* img) { (void)img; }
static void nullImageUpdate(_sc_gpu_image_t* img, const sc_gpu_image_data* d) {
    (void)img; (void)d;
}

static bool nullSamplerCreate(_sc_gpu_sampler_t* smp) { (void)smp; return true; }
static void nullSamplerDestroy(_sc_gpu_sampler_t* smp) { (void)smp; }

static bool nullShaderCreate(_sc_gpu_shader_t* shd, const sc_gpu_shader_desc* d) {
    (void)shd; (void)d; return true;
}
static void nullShaderDestroy(_sc_gpu_shader_t* shd) { (void)shd; }

static bool nullPipelineCreate(_sc_gpu_pipeline_t* pip) { (void)pip; return true; }
static void nullPipelineDestroy(_sc_gpu_pipeline_t* pip) { (void)pip; }

static void nullBeginPass(const sc_gpu_pass* pass, _sc_gpu_image_t* colors[],
                          int n, _sc_gpu_image_t* resolves[], _sc_gpu_image_t* depth) {
    (void)pass; (void)colors; (void)n; (void)resolves; (void)depth;
}
static void nullApplyViewport(int x, int y, int w, int h, bool tl) {
    (void)x; (void)y; (void)w; (void)h; (void)tl;
}
static void nullApplyScissor(int x, int y, int w, int h, bool tl) {
    (void)x; (void)y; (void)w; (void)h; (void)tl;
}
static void nullApplyPipeline(_sc_gpu_pipeline_t* pip) { (void)pip; }
static void nullApplyBindings(_sc_gpu_pipeline_t* pip, const sc_gpu_bindings* bnd,
                              _sc_gpu_buffer_t* vbufs[], _sc_gpu_buffer_t* ibuf,
                              _sc_gpu_image_t* imgs[][SC_GPU_MAX_IMAGES],
                              _sc_gpu_sampler_t* smps[][SC_GPU_MAX_SAMPLERS],
                              _sc_gpu_buffer_t* sbufs[][SC_GPU_MAX_STORAGE_BUFFERS]) {
    (void)pip; (void)bnd; (void)vbufs; (void)ibuf; (void)imgs; (void)smps; (void)sbufs;
}
static void nullApplyUniforms(int stage, int slot, const void* d, size_t sz) {
    (void)stage; (void)slot; (void)d; (void)sz;
}
static void nullDraw(int base, int count, int inst) { (void)base; (void)count; (void)inst; }
static void nullDispatch(int gx, int gy, int gz) { (void)gx; (void)gy; (void)gz; }
static void nullEndPass(void) {}
static void nullCommit(void) {}
static void nullQueryPixelformat(sc_gpu_pixel_format fmt, sc_gpu_pixelformat_info* out) {
    (void)fmt;
    out->sample = 1; out->filter = 1; out->render = 1;
    out->blend = 1; out->msaa = 1;
    out->depth = (fmt == SC_GPU_PIXELFORMAT_DEPTH || fmt == SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
}

static const _sc_gpu_backend_api nullApi = {
    .name = "null",
    .kind = SC_GPU_BACKEND_NULL,
    .init = nullInit,
    .shutdown = nullShutdown,
    .surface_create = nullSurfaceCreate,
    .surface_destroy = nullSurfaceDestroy,
    .surface_activate = nullSurfaceActivate,
    .surface_resize = nullSurfaceResize,
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

const _sc_gpu_backend_api* _sc_gpu_backend_null(void) { return &nullApi; }
