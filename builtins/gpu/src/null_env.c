/* ============================================================
 * null_env.c —— 空环境后端：全部操作成功但不触达任何硬件
 * ============================================================
 * 用途：无 GPU 环境（CI/服务器）跑通 surface 生命周期与帧循环；
 *       亦作为新后端移植的最小参照。
 * ============================================================ */

#include "internal.h"

static bool nullInit(const sc_gpu_desc* desc) { (void)desc; return true; }
static void nullShutdown(void) {}
static void* nullDevice(void) { return (void*)0; }

static bool nullSurfaceCreate(gpu_surface_t* surf) { (void)surf; return true; }
static void nullSurfaceDestroy(gpu_surface_t* surf) { (void)surf; }
static void nullSurfaceActivate(gpu_surface_t* surf) { (void)surf; }
static void nullSurfaceResize(gpu_surface_t* surf, int w, int h) {
    (void)surf; (void)w; (void)h;
}

static bool nullFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* f) {
    f->width = surf->desc.width;
    f->height = surf->desc.height;
    f->sample_count = surf->desc.sample_count;
    f->color_format = surf->desc.color_format;
    f->depth_format = surf->desc.depth_format;
    return true;
}
static void nullFrameEnd(void) {}

static const gpu_env_api nullApi = {
    .name = "null",
    .kind = SC_GPU_BACKEND_NULL,
    .init = nullInit,
    .shutdown = nullShutdown,
    .device = nullDevice,
    .surface_create = nullSurfaceCreate,
    .surface_destroy = nullSurfaceDestroy,
    .surface_activate = nullSurfaceActivate,
    .surface_resize = nullSurfaceResize,
    .frame_acquire = nullFrameAcquire,
    .frame_end = nullFrameEnd,
};

const gpu_env_api* gpu_env_null(void) { return &nullApi; }
