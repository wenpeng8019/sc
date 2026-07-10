/* ============================================================
 * vk_loader.c —— 自包含 Vulkan 加载器实现（配 ../vk_loader.h）
 * ============================================================
 * 运行时动态加载 vulkan-1.dll（Windows）/ libvulkan.so.1（Linux），
 * 经 vkGetInstanceProcAddr 解析所有入口点——免链接 vulkan-1.lib、免 SDK。
 * ============================================================ */
#include "../platform.h"
#if P_WIN || P_LINUX   /* Vulkan 仅 Windows/Linux 启用；mac 用 Metal，本 TU 空化 */
#if P_WIN
  #define VK_USE_PLATFORM_WIN32_KHR
  #include <windows.h>
#else
  #define VK_USE_PLATFORM_XLIB_KHR
  #define VK_USE_PLATFORM_WAYLAND_KHR
  #include <dlfcn.h>
#endif
#include "../vk_loader.h"

/* ---- 函数指针定义（vk_loader.h 中为 extern 声明） ---- */
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;
#define SC_VK_DEF(n) PFN_##n n = NULL;
SC_VK_GLOBAL_FUNCS(SC_VK_DEF)
SC_VK_INSTANCE_FUNCS(SC_VK_DEF)
#undef SC_VK_DEF
#if defined(VK_USE_PLATFORM_WIN32_KHR)
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = NULL;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR = NULL;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR = NULL;
#endif

static void* g_vklib = NULL;

int sc_vk_load_global(void) {
    if (!g_vklib) {
#if P_WIN
        g_vklib = (void*)LoadLibraryA("vulkan-1.dll");
#else
        g_vklib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!g_vklib) g_vklib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!g_vklib) return 0;
    }
#if P_WIN
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
        GetProcAddress((HMODULE)g_vklib, "vkGetInstanceProcAddr");
#else
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
        dlsym(g_vklib, "vkGetInstanceProcAddr");
#endif
    if (!vkGetInstanceProcAddr) return 0;
#define SC_VK_LOADG(n) n = (PFN_##n)vkGetInstanceProcAddr(NULL, #n);
    SC_VK_GLOBAL_FUNCS(SC_VK_LOADG)
#undef SC_VK_LOADG
    return vkCreateInstance != NULL;
}

void sc_vk_load_instance(VkInstance instance) {
#define SC_VK_LOADI(n) n = (PFN_##n)vkGetInstanceProcAddr(instance, #n);
    SC_VK_INSTANCE_FUNCS(SC_VK_LOADI)
#undef SC_VK_LOADI
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)
        vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    vkCreateWaylandSurfaceKHR = (PFN_vkCreateWaylandSurfaceKHR)
        vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR");
#endif
}

#endif /* P_WIN || P_LINUX */
