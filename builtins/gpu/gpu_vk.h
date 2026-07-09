/* ============================================================
 * gpu_vk.h —— gpu(env 层) 与 gfx(渲染层) 间的 Vulkan 私有契约
 * ============================================================
 * 公开 gpu.h 保持后端无关（sc_gpu_device() 返回 void*）。Vulkan 后端
 * 需要在 env 与 gfx 之间共享比 void* 更丰富的对象：设备聚合体
 * （instance/physdev/device/queue/queue_family）与每帧同步原语
 * （image_available / render_finished 信号量 + in-flight 栅栏）。
 *
 * 契约（避免污染公开 sc_gpu_frame）：
 *   · sc_gpu_device() 返回 const sc_gpu_vk_device*（gfx 强转消费）
 *   · gfx 每帧提交命令缓冲：等 wait 信号量、发 signal 信号量、栅栏 fence
 *   · env 的 frame_end 呈现：等同一 signal 信号量
 *   · sc_gpu_frame.color / .depth 携带当前交换链镜像的 VkImageView
 *
 * 仅 SC_GPU_VULKAN 编入时有效；vulkan_env.c 与 vulkan_gfx.c 共用。
 * ============================================================ */

#ifndef SC_GPU_VK_H
#define SC_GPU_VK_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备聚合体：sc_gpu_device() 在 Vulkan 后端返回本结构指针。 */
typedef struct sc_gpu_vk_device {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;
} sc_gpu_vk_device;

/* 当前在飞帧的同步原语（gfx commit 用于 vkQueueSubmit）。
 * wait   = 交换链镜像可用信号量（frame_acquire 发出，提交在颜色输出阶段等待）
 * signal = 渲染完成信号量（提交发出，env 的 frame_end 呈现时等待）
 * fence  = 在飞栅栏（提交发出，env 下一次 acquire 前等待并重置）
 * 无当前 surface 或非窗口 surface 时三者均置 VK_NULL_HANDLE。 */
void sc_gpu_vk_current_sync(VkSemaphore* out_wait,
                            VkSemaphore* out_signal,
                            VkFence*     out_fence);

/* 当前 surface 交换链的颜色格式（gfx 建渲染通道用；无则 VK_FORMAT_UNDEFINED）。 */
VkFormat sc_gpu_vk_color_format(void);
/* 当前 surface 交换链的深度格式（无深度则 VK_FORMAT_UNDEFINED）。 */
VkFormat sc_gpu_vk_depth_format(void);

#ifdef __cplusplus
}
#endif

#endif /* SC_GPU_VK_H */
