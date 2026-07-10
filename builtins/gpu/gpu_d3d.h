/* ============================================================
 * gpu_d3d.h —— gpu(env 层) 与 gfx(渲染层) 间的 Direct3D 11 私有契约
 * ============================================================
 * 公开 gpu.h 保持后端无关（sc_gpu_device() 返回 void*）。D3D11 后端
 * 经本头共享设备聚合体（ID3D11Device + 立即上下文）；每帧的渲染目标
 * （RTV/DSV）经 sc_gpu_frame.color/.depth 交付。
 *
 * 仅 SC_GPU_D3D11 编入时有效；d3d11_env.c 与 d3d11_gfx.c 共用。
 * ============================================================ */
#ifndef SC_GPU_D3D_H
#define SC_GPU_D3D_H

#ifndef COBJMACROS
#define COBJMACROS          /* D3D/DXGI 的 C 风格接口宏（ID3D11Device_XXX） */
#endif
#include <d3d11.h>
#include <dxgi1_2.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备聚合体：sc_gpu_device() 在 D3D11 后端返回本结构指针。
 * gfx 用 device 建资源/管线，用 context（立即模式）录制并提交绘制。 */
typedef struct sc_gpu_d3d_device {
    ID3D11Device*        device;
    ID3D11DeviceContext* context;
} sc_gpu_d3d_device;

#ifdef __cplusplus
}
#endif

#endif /* SC_GPU_D3D_H */
