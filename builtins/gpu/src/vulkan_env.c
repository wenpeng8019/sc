/* ============================================================
 * vulkan_env.c —— Vulkan 运行环境后端（env 层）
 * ============================================================
 * 定位：gpu_env_api 的 Vulkan 实现——实例/物理设备/逻辑设备+队列、
 *   窗口 VkSurfaceKHR（Xlib / Wayland，句柄来自 gpu.h 平台标准）、
 *   交换链、每帧镜像获取（frame_acquire）与呈现（frame_end）。
 *
 * 与 gfx 的契约见 gpu_vk.h：
 *   · sc_gpu_device() 返回 sc_gpu_vk_device*（设备聚合体）
 *   · 每帧同步原语经 sc_gpu_vk_current_sync() 交给 gfx 提交命令缓冲
 *   · sc_gpu_frame.color/.depth 携带当前交换链镜像的 VkImageView
 *
 * 平台选择（gpu 不依赖 wsi）：复刻 wsi 的 XDG/环境变量判定，令 env 与
 *   wsi 选中同一平台，native_window/native_display 语义一致对接。
 *
 * MEMORY surface / memimg（dma-buf 导出）：本后端暂不支持（置 NULL），
 *   仅实现 WINDOW 交换链路径。
 *
 * 非 Linux 目标：整文件经 SC_GPU_VULKAN 守卫空化。
 * ============================================================ */

#include "internal.h"

#ifdef SC_GPU_VULKAN

/* 平台 surface 扩展类型（须在包含 vulkan.h 前定义） */
#if P_WIN
  #define VK_USE_PLATFORM_WIN32_KHR
  #include <windows.h>
#else
  #define VK_USE_PLATFORM_XLIB_KHR
  #define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include "../gpu_vk.h"

#include <stdlib.h>
#include <string.h>

#define VK_INFLIGHT       2     /* CPU/GPU 并行帧数（与 gfx SC_GFX_MAX_INFLIGHT_FRAMES 一致） */
#define VK_MAX_SWAP_IMAGES 8

#define VKCK(expr) do { \
    VkResult vk__r = (expr); \
    if (vk__r != VK_SUCCESS) gpu_log("vulkan: %s 失败 (VkResult=%d)", #expr, (int)vk__r); \
} while (0)

/* ---- 每 surface 私有：交换链 + 深度 + 同步 ------------------ */
typedef struct VkSurfaceCtx {
    void*            native_window;
    void*            native_display;
    bool             wayland;

    VkSurfaceKHR     surface;
    VkSwapchainKHR   swapchain;
    VkFormat         colorFormat;
    VkColorSpaceKHR  colorSpace;
    VkPresentModeKHR presentMode;
    VkExtent2D       extent;

    uint32_t         imageCount;
    VkImage          images[VK_MAX_SWAP_IMAGES];
    VkImageView      views[VK_MAX_SWAP_IMAGES];

    bool             hasDepth;
    VkFormat         depthFormat;
    VkImage          depthImage;
    VkDeviceMemory   depthMem;
    VkImageView      depthView;

    VkSemaphore      imgAvail[VK_INFLIGHT];
    VkSemaphore      renderDone[VK_MAX_SWAP_IMAGES]; /* 按交换链镜像索引（present 等待） */
    VkFence          inFlight[VK_INFLIGHT];
    uint32_t         frameIndex;    /* 0..VK_INFLIGHT-1（在飞帧槽） */
    uint32_t         imageIndex;    /* 本帧 acquire 到的交换链镜像下标 */
    bool             acquired;
} VkSurfaceCtx;

/* ---- 全局 env 状态 ---------------------------------------- */
typedef struct {
    bool             valid;
    sc_gpu_vk_device dev;                 /* device() 返回本结构指针 */
    bool             hasXlib;
    bool             hasWayland;
    bool             hasWin32;
    VkSurfaceCtx*    cur;                  /* 当前 surface（sync 访问器用） */
} VkEnv;

static VkEnv g_vk;

/* ============================================================
 * 工具
 * ============================================================ */

static bool ext_present(const VkExtensionProperties* list, uint32_t n, const char* name) {
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(list[i].extensionName, name) == 0) return true;
    return false;
}

/* 复刻 wsi 平台判定：优先 XDG_SESSION_TYPE，退化按 WAYLAND_DISPLAY 存在性。 */
#if !P_WIN
static bool prefer_wayland(void) {
    const char* session = getenv("XDG_SESSION_TYPE");
    const char* wl  = getenv("WAYLAND_DISPLAY");
    const char* dpy = getenv("DISPLAY");
    if (session) {
        if (strcmp(session, "wayland") == 0 && wl && wl[0]) return true;
        if (strcmp(session, "x11") == 0 && dpy && dpy[0]) return false;
    }
    if (wl && wl[0]) return true;   /* wsi 无 XDG 时先试 Wayland */
    return false;                   /* 否则 X11 */
}
#endif

/* sc_gpu 像素格式 → VkFormat（颜色）。 */
static VkFormat vk_color_format(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case SC_GPU_PIXELFORMAT_SRGB8A8:  return VK_FORMAT_R8G8B8A8_SRGB;
        case SC_GPU_PIXELFORMAT_BGRA8:    return VK_FORMAT_B8G8R8A8_UNORM;
        case SC_GPU_PIXELFORMAT_RGB10A2:  return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case SC_GPU_PIXELFORMAT_RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SC_GPU_PIXELFORMAT_RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SC_GPU_PIXELFORMAT_R8:       return VK_FORMAT_R8_UNORM;
        case SC_GPU_PIXELFORMAT_RG8:      return VK_FORMAT_R8G8_UNORM;
        default:                          return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

static uint32_t find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_vk.dev.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

static VkFormat pick_depth_format(sc_gpu_pixel_format want) {
    if (want == SC_GPU_PIXELFORMAT_NONE) return VK_FORMAT_UNDEFINED;
    VkFormat cand[3];
    int n = 0;
    if (want == SC_GPU_PIXELFORMAT_DEPTH) {
        cand[n++] = VK_FORMAT_D32_SFLOAT;
        cand[n++] = VK_FORMAT_D32_SFLOAT_S8_UINT;
    } else { /* DEPTH_STENCIL */
        cand[n++] = VK_FORMAT_D32_SFLOAT_S8_UINT;
        cand[n++] = VK_FORMAT_D24_UNORM_S8_UINT;
    }
    for (int i = 0; i < n; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vk.dev.phys, cand[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return cand[i];
    }
    return VK_FORMAT_UNDEFINED;
}

/* ============================================================
 * 初始化 / 收尾
 * ============================================================ */

static bool vkInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset(&g_vk, 0, sizeof(g_vk));

    /* --- 实例扩展 --- */
    uint32_t nExt = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &nExt, NULL);
    VkExtensionProperties* exts = (VkExtensionProperties*)calloc(nExt ? nExt : 1, sizeof(*exts));
    vkEnumerateInstanceExtensionProperties(NULL, &nExt, exts);

    const char* wanted[8];
    uint32_t nWanted = 0;
    if (ext_present(exts, nExt, VK_KHR_SURFACE_EXTENSION_NAME))
        wanted[nWanted++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if P_WIN
    if (ext_present(exts, nExt, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        wanted[nWanted++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        g_vk.hasWin32 = true;
    }
#else
    if (ext_present(exts, nExt, VK_KHR_XLIB_SURFACE_EXTENSION_NAME)) {
        wanted[nWanted++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
        g_vk.hasXlib = true;
    }
    if (ext_present(exts, nExt, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
        wanted[nWanted++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        g_vk.hasWayland = true;
    }
#endif
    free(exts);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "sc-gpu";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "sc";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = nWanted;
    ici.ppEnabledExtensionNames = wanted;

    /* 可选：GPU_VK_VALIDATE=1 且层可用时启用校验层 */
    const char* valLayer = "VK_LAYER_KHRONOS_validation";
    const char* layers[1] = { valLayer };
    if (getenv("GPU_VK_VALIDATE")) {
        uint32_t nLayer = 0;
        vkEnumerateInstanceLayerProperties(&nLayer, NULL);
        VkLayerProperties* lp = (VkLayerProperties*)calloc(nLayer ? nLayer : 1, sizeof(*lp));
        vkEnumerateInstanceLayerProperties(&nLayer, lp);
        bool have = false;
        for (uint32_t i = 0; i < nLayer; i++)
            if (strcmp(lp[i].layerName, valLayer) == 0) { have = true; break; }
        free(lp);
        if (have) {
            ici.enabledLayerCount = 1;
            ici.ppEnabledLayerNames = layers;
        }
    }

    if (vkCreateInstance(&ici, NULL, &g_vk.dev.instance) != VK_SUCCESS) {
        gpu_log("vulkan: vkCreateInstance 失败");
        return false;
    }

    /* --- 物理设备（优先独显，退化取首个） --- */
    uint32_t nPhys = 0;
    vkEnumeratePhysicalDevices(g_vk.dev.instance, &nPhys, NULL);
    if (nPhys == 0) {
        gpu_log("vulkan: 无可用物理设备");
        vkDestroyInstance(g_vk.dev.instance, NULL);
        g_vk.dev.instance = VK_NULL_HANDLE;
        return false;
    }
    VkPhysicalDevice* phys = (VkPhysicalDevice*)calloc(nPhys, sizeof(*phys));
    vkEnumeratePhysicalDevices(g_vk.dev.instance, &nPhys, phys);
    g_vk.dev.phys = phys[0];
    for (uint32_t i = 0; i < nPhys; i++) {
        VkPhysicalDeviceProperties pd;
        vkGetPhysicalDeviceProperties(phys[i], &pd);
        if (pd.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            g_vk.dev.phys = phys[i];
            break;
        }
    }
    free(phys);

    VkPhysicalDeviceProperties pdp;
    vkGetPhysicalDeviceProperties(g_vk.dev.phys, &pdp);
    gpu_log("vulkan: 设备 = %s (API %u.%u.%u)", pdp.deviceName,
            VK_VERSION_MAJOR(pdp.apiVersion), VK_VERSION_MINOR(pdp.apiVersion),
            VK_VERSION_PATCH(pdp.apiVersion));

    /* --- 队列族：选支持 GRAPHICS 的（假定同族可呈现，单 GPU 成立） --- */
    uint32_t nQF = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.dev.phys, &nQF, NULL);
    VkQueueFamilyProperties* qf = (VkQueueFamilyProperties*)calloc(nQF ? nQF : 1, sizeof(*qf));
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.dev.phys, &nQF, qf);
    g_vk.dev.queue_family = 0;
    bool found = false;
    for (uint32_t i = 0; i < nQF; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            g_vk.dev.queue_family = i;
            found = true;
            break;
        }
    }
    free(qf);
    if (!found) {
        gpu_log("vulkan: 无图形队列族");
        vkDestroyInstance(g_vk.dev.instance, NULL);
        memset(&g_vk, 0, sizeof(g_vk));
        return false;
    }

    /* --- 逻辑设备 + 队列 --- */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    dqci.queueFamilyIndex = g_vk.dev.queue_family;
    dqci.queueCount = 1;
    dqci.pQueuePriorities = &prio;

    const char* devExts[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(g_vk.dev.phys, &dci, NULL, &g_vk.dev.device) != VK_SUCCESS) {
        gpu_log("vulkan: vkCreateDevice 失败");
        vkDestroyInstance(g_vk.dev.instance, NULL);
        memset(&g_vk, 0, sizeof(g_vk));
        return false;
    }
    vkGetDeviceQueue(g_vk.dev.device, g_vk.dev.queue_family, 0, &g_vk.dev.queue);

    g_vk.valid = true;
    return true;
}

static void vkShutdown(void) {
    if (!g_vk.valid) return;
    if (g_vk.dev.device) vkDeviceWaitIdle(g_vk.dev.device);
    if (g_vk.dev.device) vkDestroyDevice(g_vk.dev.device, NULL);
    if (g_vk.dev.instance) vkDestroyInstance(g_vk.dev.instance, NULL);
    memset(&g_vk, 0, sizeof(g_vk));
}

static void* vkDeviceFn(void) { return &g_vk.dev; }

/* ============================================================
 * 交换链构建 / 销毁
 * ============================================================ */

static void destroy_swapchain_res(VkSurfaceCtx* c) {
    VkDevice d = g_vk.dev.device;
    if (c->depthView)  { vkDestroyImageView(d, c->depthView, NULL);  c->depthView = VK_NULL_HANDLE; }
    if (c->depthImage) { vkDestroyImage(d, c->depthImage, NULL);     c->depthImage = VK_NULL_HANDLE; }
    if (c->depthMem)   { vkFreeMemory(d, c->depthMem, NULL);         c->depthMem = VK_NULL_HANDLE; }
    for (uint32_t i = 0; i < c->imageCount; i++) {
        if (c->views[i]) { vkDestroyImageView(d, c->views[i], NULL); c->views[i] = VK_NULL_HANDLE; }
    }
    c->imageCount = 0;
    if (c->swapchain) { vkDestroySwapchainKHR(d, c->swapchain, NULL); c->swapchain = VK_NULL_HANDLE; }
}

static bool create_swapchain(VkSurfaceCtx* c, int w, int h, sc_gpu_pixel_format wantColor,
                             sc_gpu_pixel_format wantDepth) {
    VkDevice d = g_vk.dev.device;
    VkPhysicalDevice pd = g_vk.dev.phys;

    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, c->surface, &caps) != VK_SUCCESS) {
        gpu_log("vulkan: 查询 surface 能力失败");
        return false;
    }

    /* 颜色格式：优先想要的，退化取首个可用 */
    uint32_t nFmt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, c->surface, &nFmt, NULL);
    if (nFmt == 0) { gpu_log("vulkan: surface 无可用格式"); return false; }
    VkSurfaceFormatKHR* fmts = (VkSurfaceFormatKHR*)calloc(nFmt, sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, c->surface, &nFmt, fmts);
    VkFormat desired = vk_color_format(wantColor);
    c->colorFormat = fmts[0].format;
    c->colorSpace  = fmts[0].colorSpace;
    for (uint32_t i = 0; i < nFmt; i++) {
        if (fmts[i].format == desired) {
            c->colorFormat = fmts[i].format;
            c->colorSpace  = fmts[i].colorSpace;
            break;
        }
    }
    free(fmts);

    /* 呈现模式：优先 FIFO（保证可用，vsync） */
    c->presentMode = VK_PRESENT_MODE_FIFO_KHR;

    /* 尺寸 */
    VkExtent2D ext;
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        ext = caps.currentExtent;
    } else {
        ext.width  = (uint32_t)(w > 0 ? w : 1);
        ext.height = (uint32_t)(h > 0 ? h : 1);
        if (ext.width  < caps.minImageExtent.width)  ext.width  = caps.minImageExtent.width;
        if (ext.width  > caps.maxImageExtent.width)  ext.width  = caps.maxImageExtent.width;
        if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
        if (ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
    }
    if (ext.width == 0 || ext.height == 0) return false;   /* 最小化窗口 */
    c->extent = ext;

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && want > caps.maxImageCount) want = caps.maxImageCount;
    if (want > VK_MAX_SWAP_IMAGES) want = VK_MAX_SWAP_IMAGES;

    VkSwapchainCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sci.surface = c->surface;
    sci.minImageCount = want;
    sci.imageFormat = c->colorFormat;
    sci.imageColorSpace = c->colorSpace;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                       ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = c->presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(d, &sci, NULL, &c->swapchain) != VK_SUCCESS) {
        gpu_log("vulkan: vkCreateSwapchainKHR 失败");
        return false;
    }

    vkGetSwapchainImagesKHR(d, c->swapchain, &c->imageCount, NULL);
    if (c->imageCount > VK_MAX_SWAP_IMAGES) c->imageCount = VK_MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(d, c->swapchain, &c->imageCount, c->images);

    for (uint32_t i = 0; i < c->imageCount; i++) {
        VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        iv.image = c->images[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = c->colorFormat;
        iv.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        VKCK(vkCreateImageView(d, &iv, NULL, &c->views[i]));
    }

    /* 深度附件 */
    c->depthFormat = pick_depth_format(wantDepth);
    c->hasDepth = (c->depthFormat != VK_FORMAT_UNDEFINED);
    if (c->hasDepth) {
        VkImageCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = c->depthFormat;
        ic.extent.width = ext.width;
        ic.extent.height = ext.height;
        ic.extent.depth = 1;
        ic.mipLevels = 1;
        ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VKCK(vkCreateImage(d, &ic, NULL, &c->depthImage));

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(d, c->depthImage, &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKCK(vkAllocateMemory(d, &mai, NULL, &c->depthMem));
        VKCK(vkBindImageMemory(d, c->depthImage, c->depthMem, 0));

        bool stencil = (c->depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                        c->depthFormat == VK_FORMAT_D24_UNORM_S8_UINT);
        VkImageViewCreateInfo dv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        dv.image = c->depthImage;
        dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dv.format = c->depthFormat;
        dv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
            (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        dv.subresourceRange.levelCount = 1;
        dv.subresourceRange.layerCount = 1;
        VKCK(vkCreateImageView(d, &dv, NULL, &c->depthView));
    }
    return true;
}

/* ============================================================
 * surface 生命周期
 * ============================================================ */

static bool vkSurfaceCreate(gpu_surface_t* surf) {
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        gpu_log("vulkan: MEMORY surface（memimg 环）暂不支持");
        return false;
    }
    if (!surf->desc.native_window) {
        gpu_log("vulkan: WINDOW surface 缺 native_window");
        return false;
    }

    VkSurfaceCtx* c = (VkSurfaceCtx*)calloc(1, sizeof(VkSurfaceCtx));
    if (!c) return false;
    c->native_window = surf->desc.native_window;
    c->native_display = surf->desc.native_display;

    /* --- 创建 VkSurfaceKHR（按平台） --- */
#if P_WIN
    c->wayland = false;
    if (g_vk.hasWin32) {
        VkWin32SurfaceCreateInfoKHR w32 = { .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        w32.hinstance = GetModuleHandle(NULL);
        w32.hwnd = (HWND)c->native_window;
        if (vkCreateWin32SurfaceKHR(g_vk.dev.instance, &w32, NULL, &c->surface) != VK_SUCCESS) {
            gpu_log("vulkan: vkCreateWin32SurfaceKHR 失败");
            free(c);
            return false;
        }
    } else {
        gpu_log("vulkan: 无可用窗口 surface 扩展");
        free(c);
        return false;
    }
#else
    c->wayland = prefer_wayland();
    if (c->wayland && g_vk.hasWayland) {
        VkWaylandSurfaceCreateInfoKHR wsci = { .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
        wsci.display = (struct wl_display*)c->native_display;
        wsci.surface = (struct wl_surface*)c->native_window;
        if (vkCreateWaylandSurfaceKHR(g_vk.dev.instance, &wsci, NULL, &c->surface) != VK_SUCCESS) {
            gpu_log("vulkan: vkCreateWaylandSurfaceKHR 失败");
            free(c);
            return false;
        }
    } else if (g_vk.hasXlib) {
        VkXlibSurfaceCreateInfoKHR xsci = { .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
        xsci.dpy = (Display*)c->native_display;
        xsci.window = (Window)(uintptr_t)c->native_window;
        if (vkCreateXlibSurfaceKHR(g_vk.dev.instance, &xsci, NULL, &c->surface) != VK_SUCCESS) {
            gpu_log("vulkan: vkCreateXlibSurfaceKHR 失败");
            free(c);
            return false;
        }
        c->wayland = false;
    } else {
        gpu_log("vulkan: 无可用窗口 surface 扩展");
        free(c);
        return false;
    }
#endif

    /* 确认队列族支持呈现 */
    VkBool32 sup = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.dev.phys, g_vk.dev.queue_family, c->surface, &sup);
    if (!sup) gpu_log("vulkan: 警告——队列族不支持呈现，可能失败");

    if (!create_swapchain(c, surf->desc.width, surf->desc.height,
                          surf->desc.color_format, surf->desc.depth_format)) {
        vkDestroySurfaceKHR(g_vk.dev.instance, c->surface, NULL);
        free(c);
        return false;
    }

    /* 同步原语 */
    /* 同步原语：imgAvail/inFlight 按在飞帧槽；renderDone（present 等待）按
     * 交换链镜像索引——避免信号量在呈现操作仍挂起时被复用而复被 signal
     * (VUID-vkQueueSubmit-pSignalSemaphores-00067)。预建满 VK_MAX_SWAP_IMAGES
     * 个，交换链重建即使镜像数变化也不缺。 */
    VkSemaphoreCreateInfo semci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < VK_INFLIGHT; i++) {
        VKCK(vkCreateSemaphore(g_vk.dev.device, &semci, NULL, &c->imgAvail[i]));
        VKCK(vkCreateFence(g_vk.dev.device, &fci, NULL, &c->inFlight[i]));
    }
    for (int i = 0; i < VK_MAX_SWAP_IMAGES; i++)
        VKCK(vkCreateSemaphore(g_vk.dev.device, &semci, NULL, &c->renderDone[i]));
    c->frameIndex = 0;
    c->acquired = false;

    surf->backend = c;
    /* 首个/默认 surface 即当前 */
    if (!g_vk.cur) g_vk.cur = c;
    gpu_log("vulkan: surface 就绪 (%s, %ux%u, %u 镜像)",
#if P_WIN
            "win32",
#else
            c->wayland ? "wayland" : "x11",
#endif
            c->extent.width, c->extent.height, c->imageCount);
    return true;
}

static void vkSurfaceDestroy(gpu_surface_t* surf) {
    VkSurfaceCtx* c = (VkSurfaceCtx*)surf->backend;
    if (!c) return;
    VkDevice d = g_vk.dev.device;
    if (d) vkDeviceWaitIdle(d);
    destroy_swapchain_res(c);
    for (int i = 0; i < VK_INFLIGHT; i++) {
        if (c->imgAvail[i])   vkDestroySemaphore(d, c->imgAvail[i], NULL);
        if (c->inFlight[i])   vkDestroyFence(d, c->inFlight[i], NULL);
    }
    for (int i = 0; i < VK_MAX_SWAP_IMAGES; i++)
        if (c->renderDone[i]) vkDestroySemaphore(d, c->renderDone[i], NULL);
    if (c->surface) vkDestroySurfaceKHR(g_vk.dev.instance, c->surface, NULL);
    if (g_vk.cur == c) g_vk.cur = NULL;
    free(c);
    surf->backend = NULL;
}

static void vkSurfaceActivate(gpu_surface_t* surf) {
    g_vk.cur = (VkSurfaceCtx*)surf->backend;
}

static void recreate_swapchain(VkSurfaceCtx* c, gpu_surface_t* surf) {
    vkDeviceWaitIdle(g_vk.dev.device);
    destroy_swapchain_res(c);
    create_swapchain(c, surf->desc.width, surf->desc.height,
                     surf->desc.color_format, surf->desc.depth_format);
}

static void vkSurfaceResize(gpu_surface_t* surf, int w, int h) {
    VkSurfaceCtx* c = (VkSurfaceCtx*)surf->backend;
    if (!c) return;
    surf->desc.width = w;
    surf->desc.height = h;
    recreate_swapchain(c, surf);
}

/* ============================================================
 * 帧交付
 * ============================================================ */

static bool vkFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* out) {
    VkSurfaceCtx* c = (VkSurfaceCtx*)surf->backend;
    if (!c || !c->swapchain) return false;
    g_vk.cur = c;
    VkDevice d = g_vk.dev.device;

    uint32_t fi = c->frameIndex;
    vkWaitForFences(d, 1, &c->inFlight[fi], VK_TRUE, UINT64_MAX);

    VkResult r = vkAcquireNextImageKHR(d, c->swapchain, UINT64_MAX,
                                       c->imgAvail[fi], VK_NULL_HANDLE, &c->imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(c, surf);
        r = vkAcquireNextImageKHR(d, c->swapchain, UINT64_MAX,
                                  c->imgAvail[fi], VK_NULL_HANDLE, &c->imageIndex);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        gpu_log("vulkan: acquire 失败 (VkResult=%d)", (int)r);
        return false;
    }
    vkResetFences(d, 1, &c->inFlight[fi]);
    c->acquired = true;

    memset(out, 0, sizeof(*out));
    out->color = (void*)(uintptr_t)c->views[c->imageIndex];
    out->depth = c->hasDepth ? (void*)(uintptr_t)c->depthView : NULL;
    out->width = (int)c->extent.width;
    out->height = (int)c->extent.height;
    out->sample_count = 1;
    out->color_format = surf->desc.color_format;
    out->depth_format = c->hasDepth ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;
    return true;
}

static void vkFrameEnd(void) {
    VkSurfaceCtx* c = g_vk.cur;
    if (!c || !c->acquired) return;
    uint32_t fi = c->frameIndex;

    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &c->renderDone[c->imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &c->swapchain;
    pi.pImageIndices = &c->imageIndex;

    VkResult r = vkQueuePresentKHR(g_vk.dev.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        /* 下帧 acquire 时重建（此处仅标记：直接重建以简化） */
        vkDeviceWaitIdle(g_vk.dev.device);
        destroy_swapchain_res(c);
        /* 用现有 extent 重建（尺寸从 caps 取） */
        VkSurfaceCapabilitiesKHR caps;
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.dev.phys, c->surface, &caps) == VK_SUCCESS &&
            caps.currentExtent.width != 0xFFFFFFFFu) {
            create_swapchain(c, (int)caps.currentExtent.width, (int)caps.currentExtent.height,
                             SC_GPU_PIXELFORMAT_DEFAULT, SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
        }
    }
    c->frameIndex = (fi + 1) % VK_INFLIGHT;
    c->acquired = false;
}

/* ============================================================
 * gpu_vk.h 契约实现（gfx 消费）
 * ============================================================ */

void sc_gpu_vk_current_sync(VkSemaphore* out_wait, VkSemaphore* out_signal, VkFence* out_fence) {
    VkSurfaceCtx* c = g_vk.cur;
    if (c) {
        uint32_t fi = c->frameIndex;
        if (out_wait)   *out_wait   = c->imgAvail[fi];
        if (out_signal) *out_signal = c->renderDone[c->imageIndex];
        if (out_fence)  *out_fence  = c->inFlight[fi];
    } else {
        if (out_wait)   *out_wait   = VK_NULL_HANDLE;
        if (out_signal) *out_signal = VK_NULL_HANDLE;
        if (out_fence)  *out_fence  = VK_NULL_HANDLE;
    }
}

VkFormat sc_gpu_vk_color_format(void) {
    return g_vk.cur ? g_vk.cur->colorFormat : VK_FORMAT_UNDEFINED;
}

VkFormat sc_gpu_vk_depth_format(void) {
    return (g_vk.cur && g_vk.cur->hasDepth) ? g_vk.cur->depthFormat : VK_FORMAT_UNDEFINED;
}

/* ============================================================
 * vtable
 * ============================================================ */

static const gpu_env_api vulkanApi = {
    .name = "vulkan",
    .kind = SC_GPU_BACKEND_VULKAN,
    .init = vkInit,
    .shutdown = vkShutdown,
    .device = vkDeviceFn,
    .surface_create = vkSurfaceCreate,
    .surface_destroy = vkSurfaceDestroy,
    .surface_activate = vkSurfaceActivate,
    .surface_resize = vkSurfaceResize,
    .frame_acquire = vkFrameAcquire,
    .frame_end = vkFrameEnd,
    /* memimg / MEMORY surface 暂不支持：全 NULL */
};

const gpu_env_api* gpu_env_vulkan(void) { return &vulkanApi; }

#endif /* SC_GPU_VULKAN */
