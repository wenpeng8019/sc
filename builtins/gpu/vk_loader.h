/* ============================================================
 * vk_loader.h —— 自包含 Vulkan 加载器（volk 精简版，仅覆盖本项目用到的入口）
 * ============================================================
 * 目的：VK_NO_PROTOTYPES + 运行时动态加载 vulkan-1.dll / libvulkan.so.1，
 *   免链接 vulkan-1.lib、免 Vulkan SDK（头文件已 vendor 至 builtins/gpu/khr/）。
 * 用法：包含前按平台 #define VK_USE_PLATFORM_{WIN32,XLIB,WAYLAND}_KHR（同 vulkan.h）。
 *   sc_vk_load_global() 装载库 + 全局函数；vkCreateInstance 后 sc_vk_load_instance()。
 * 本文件由脚本从 vulkan_env.c/vulkan_gfx.c 实际调用集生成（勿手改函数列表）。
 * ============================================================ */
#ifndef SC_VK_LOADER_H
#define SC_VK_LOADER_H

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 全局函数（vkGetInstanceProcAddr(NULL) 装载） */
#define SC_VK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance) \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties) \

/* 实例/设备函数（vkGetInstanceProcAddr(instance) 装载） */
#define SC_VK_INSTANCE_FUNCS(X) \
    X(vkAcquireNextImageKHR) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdDispatch) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdEndRenderPass) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdSetScissor) \
    X(vkCmdSetViewport) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateComputePipelines) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateDevice) \
    X(vkCreateFence) \
    X(vkCreateFramebuffer) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineLayout) \
    X(vkCreateRenderPass) \
    X(vkCreateSampler) \
    X(vkCreateSemaphore) \
    X(vkCreateShaderModule) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyDevice) \
    X(vkDestroyFence) \
    X(vkDestroyFramebuffer) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyInstance) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyRenderPass) \
    X(vkDestroySampler) \
    X(vkDestroySemaphore) \
    X(vkDestroyShaderModule) \
    X(vkDestroySurfaceKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkDeviceWaitIdle) \
    X(vkEndCommandBuffer) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkEnumeratePhysicalDevices) \
    X(vkFreeCommandBuffers) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceProcAddr) \
    X(vkGetDeviceQueue) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetPhysicalDeviceFeatures2) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkMapMemory) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkResetCommandBuffer) \
    X(vkResetFences) \
    X(vkUnmapMemory) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences) \

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define SC_VK_DECL(n) extern PFN_##n n;
SC_VK_GLOBAL_FUNCS(SC_VK_DECL)
SC_VK_INSTANCE_FUNCS(SC_VK_DECL)
#undef SC_VK_DECL
#if defined(VK_USE_PLATFORM_WIN32_KHR)
extern PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
extern PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
extern PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#endif
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
extern PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif

/* 装载 vulkan-1.dll/libvulkan + 全局函数；返回 1=成功、0=无 Vulkan 运行时。 */
int  sc_vk_load_global(void);
/* 实例创建后装载其余函数（含设备级，经实例派发）。 */
void sc_vk_load_instance(VkInstance instance);

#ifdef __cplusplus
}
#endif
#endif /* SC_VK_LOADER_H */
