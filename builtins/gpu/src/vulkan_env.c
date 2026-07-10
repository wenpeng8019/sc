/* ============================================================
 * vulkan_env.c —— Vulkan 运行环境后端（env 层）
 * ============================================================
 * 定位：gpu_env_api 的 Vulkan 实现——实例/物理设备/逻辑设备+队列、
 *   窗口 VkSurfaceKHR（Win32 / Xlib / Wayland，句柄来自 gpu.h 平台标准）、
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
 * MEMORY surface / memimg（无屏）：已实现，纯核心 Vulkan（离屏渲染到 VkImage +
 *   host-visible staging 拷贝回读），全平台通用。零拷贝导出已实现——vkMemimgExport
 *   经外部内存扩展导出句柄（Linux dma-buf fd via VK_KHR_external_memory_fd /
 *   Windows NT 句柄 via win32；设备支持则启用，可导出内存=专用分配）。
 *   Windows NT 句柄路径已实测；导入（vkMemimgImport）与 Linux dma-buf 实机验证待补。
 *
 * 平台守卫：整文件经 SC_GPU_VULKAN 空化（Windows/Linux 启用，macOS 用 Metal）。
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
  #include <unistd.h>                 /* close()（dma-buf 导出 fd 释放） */
#endif
#include "../gpu_vk.h"

#include <stdlib.h>
#include <string.h>

#define VK_INFLIGHT       2     /* CPU/GPU 并行帧数（与 gfx SC_GFX_MAX_INFLIGHT_FRAMES 一致） */
#define VK_MAX_SWAP_IMAGES 8

/* memimg 零拷贝导出的外部内存句柄类型（平台对偶）：
 * Linux = dma-buf（可导 GL/v4l2/编码器）；Windows = 不透明 NT 句柄（D3D/编码器互操作）。 */
#if P_WIN
  #define SC_VK_EXTMEM_HANDLE_TYPE VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
#else
  #define SC_VK_EXTMEM_HANDLE_TYPE VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
#endif

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
    bool             memory;        /* MEMORY surface（无表面/离屏，memimg 环） */
} VkSurfaceCtx;

/* ---- 全局 env 状态 ---------------------------------------- */
typedef struct {
    bool             valid;
    sc_gpu_vk_device dev;                 /* device() 返回本结构指针 */
    bool             hasXlib;
    bool             hasWayland;
    bool             hasWin32;
    VkSurfaceCtx*    cur;                  /* 当前 surface（sync 访问器用） */
    VkCommandPool    utilPool;             /* 一次性命令（memimg 布局转换/回读拷贝） */
    bool             hasExtMem;            /* 外部内存导出扩展可用（dma-buf/win32 NT 句柄） */
#if P_WIN
    PFN_vkGetMemoryWin32HandleKHR getMemWin32;
#else
    PFN_vkGetMemoryFdKHR          getMemFd;
#endif
} VkEnv;

static VkEnv g_vk;

/* ---- memimg 私有：离屏渲染目标 VkImage + 回读 staging ------- */
typedef struct VkMemimg {
    VkImage        image;
    VkDeviceMemory mem;
    VkImageView    view;      /* env 自有 view（gfx Mode B 另建借用 view） */
    VkFormat       format;
    int            w, h;
    VkBuffer       staging;   /* 回读用 host-visible 缓冲（懒建） */
    VkDeviceMemory stagingMem;
    void*          mapped;    /* staging 持久映射 */
    bool           exportable; /* 分配为可导出（external memory + dedicated） */
    int            exportFd;   /* linux: 缓存的 dma-buf fd（借用，free 时 close）；-1=无 */
    void*          exportHandle; /* windows: 缓存的 NT 句柄（借用，free 时 CloseHandle）；NULL=无 */
} VkMemimg;

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

    /* 自包含：动态加载 Vulkan 运行时（vulkan-1.dll / libvulkan.so.1）+ 全局函数。
     * 无运行时（无驱动/未装）则优雅失败，pickBackend 回退其他后端。 */
    if (!sc_vk_load_global()) {
        gpu_log("vulkan: 无 Vulkan 运行时（vulkan-1.dll / libvulkan 缺失或过旧）");
        return false;
    }

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

    /* 实例已建：装载其余（实例/设备级）函数指针 */
    sc_vk_load_instance(g_vk.dev.instance);

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

    const char* devExts[8];
    uint32_t nDE = 0;
    devExts[nDE++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    /* 设备扩展：（可用则）启用外部内存导出——Linux dma-buf / Windows NT 句柄 */
    uint32_t nDevExt = 0;
    vkEnumerateDeviceExtensionProperties(g_vk.dev.phys, NULL, &nDevExt, NULL);
    VkExtensionProperties* devExtList =
        (VkExtensionProperties*)calloc(nDevExt ? nDevExt : 1, sizeof(*devExtList));
    vkEnumerateDeviceExtensionProperties(g_vk.dev.phys, NULL, &nDevExt, devExtList);
    bool haveExtMem = ext_present(devExtList, nDevExt, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
#if P_WIN
    if (haveExtMem && ext_present(devExtList, nDevExt, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)) {
        devExts[nDE++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
        devExts[nDE++] = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
        g_vk.hasExtMem = true;
    }
#else
    if (haveExtMem &&
        ext_present(devExtList, nDevExt, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
        ext_present(devExtList, nDevExt, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)) {
        devExts[nDE++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
        devExts[nDE++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        devExts[nDE++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
        g_vk.hasExtMem = true;
    }
#endif
    free(devExtList);

    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqci;
    dci.enabledExtensionCount = nDE;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(g_vk.dev.phys, &dci, NULL, &g_vk.dev.device) != VK_SUCCESS) {
        gpu_log("vulkan: vkCreateDevice 失败");
        vkDestroyInstance(g_vk.dev.instance, NULL);
        memset(&g_vk, 0, sizeof(g_vk));
        return false;
    }
    vkGetDeviceQueue(g_vk.dev.device, g_vk.dev.queue_family, 0, &g_vk.dev.queue);

    /* 加载外部内存导出函数指针（扩展函数，经 vkGetDeviceProcAddr） */
    if (g_vk.hasExtMem) {
#if P_WIN
        g_vk.getMemWin32 = (PFN_vkGetMemoryWin32HandleKHR)
            vkGetDeviceProcAddr(g_vk.dev.device, "vkGetMemoryWin32HandleKHR");
        if (!g_vk.getMemWin32) g_vk.hasExtMem = false;
#else
        g_vk.getMemFd = (PFN_vkGetMemoryFdKHR)
            vkGetDeviceProcAddr(g_vk.dev.device, "vkGetMemoryFdKHR");
        if (!g_vk.getMemFd) g_vk.hasExtMem = false;
#endif
        if (g_vk.hasExtMem) gpu_log("vulkan: memimg 零拷贝导出可用（外部内存扩展）");
    }

    /* 一次性命令池（memimg 布局转换 / 回读拷贝用；瞬时缓冲，可复位） */
    VkCommandPoolCreateInfo upci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    upci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    upci.queueFamilyIndex = g_vk.dev.queue_family;
    vkCreateCommandPool(g_vk.dev.device, &upci, NULL, &g_vk.utilPool);

    g_vk.valid = true;
    return true;
}

static void vkShutdown(void) {
    if (!g_vk.valid) return;
    if (g_vk.dev.device) vkDeviceWaitIdle(g_vk.dev.device);
    if (g_vk.utilPool) vkDestroyCommandPool(g_vk.dev.device, g_vk.utilPool, NULL);
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
 * memimg（离屏内存图像：VkImage + 回读 staging）
 * ============================================================ */

/* 一次性命令：utilPool 分配 → 录制 → 提交 → 等空闲 → 释放。 */
static VkCommandBuffer vk_oneshot_begin(void) {
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = g_vk.utilPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(g_vk.dev.device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void vk_oneshot_end(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(g_vk.dev.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_vk.dev.queue);
    vkFreeCommandBuffers(g_vk.dev.device, g_vk.utilPool, 1, &cmd);
}

static bool vkMemimgAlloc(gpu_memimg_t* img) {
    const sc_gpu_memimg_desc* d = &img->desc;
    VkMemimg* m = (VkMemimg*)calloc(1, sizeof(VkMemimg));
    if (!m) return false;
    VkDevice dev = g_vk.dev.device;
    m->w = d->width; m->h = d->height;
    m->format = vk_color_format(d->format);
    m->exportFd = -1;

    VkImageCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = m->format;
    ic.extent.width = (uint32_t)(m->w > 0 ? m->w : 1);
    ic.extent.height = (uint32_t)(m->h > 0 ? m->h : 1);
    ic.extent.depth = 1;
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    /* 可导出：声明外部内存句柄类型（dma-buf / win32 NT 句柄） */
    VkExternalMemoryImageCreateInfo extImg = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    if (g_vk.hasExtMem) {
        extImg.handleTypes = SC_VK_EXTMEM_HANDLE_TYPE;
        ic.pNext = &extImg;
        m->exportable = true;
    }
    if (vkCreateImage(dev, &ic, NULL, &m->image) != VK_SUCCESS) { free(m); return false; }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, m->image, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    /* 可导出内存：外部句柄要求专用分配（VkMemoryDedicatedAllocateInfo）+ 导出信息。 */
    VkMemoryDedicatedAllocateInfo dedic = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    VkExportMemoryAllocateInfo expo = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    if (m->exportable) {
        dedic.image = m->image;
        expo.handleTypes = SC_VK_EXTMEM_HANDLE_TYPE;
        expo.pNext = &dedic;
        mai.pNext = &expo;
    }
    if (vkAllocateMemory(dev, &mai, NULL, &m->mem) != VK_SUCCESS) {
        vkDestroyImage(dev, m->image, NULL); free(m); return false;
    }
    vkBindImageMemory(dev, m->image, m->mem, 0);

    VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image = m->image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = m->format;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    vkCreateImageView(dev, &iv, NULL, &m->view);

    /* UNDEFINED → TRANSFER_SRC_OPTIMAL：离屏 renderpass 与 map 拷贝统一以此为基态，
     * map 前无需再 barrier（离屏 pass finalLayout 亦为 TRANSFER_SRC）。 */
    VkCommandBuffer cmd = vk_oneshot_begin();
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m->image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
    vk_oneshot_end(cmd);

    img->backend = m;
    return true;
}

static bool vkMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    (void)img; (void)src;
    gpu_log("vulkan: memimg import 暂不支持（需外部内存导入路径）");
    return false;
}

/* 懒导出外部内存句柄（缓存；借用语义，memimg_free 时关闭）。
 * Linux → dma-buf fd（VK_KHR_external_memory_fd）；Windows → NT 句柄（win32）。 */
static void memimg_ensure_export(VkMemimg* m) {
    if (!m->exportable || !g_vk.hasExtMem) return;
#if P_WIN
    if (m->exportHandle) return;
    VkMemoryGetWin32HandleInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    gi.memory = m->mem;
    gi.handleType = SC_VK_EXTMEM_HANDLE_TYPE;
    HANDLE h = NULL;
    if (g_vk.getMemWin32(g_vk.dev.device, &gi, &h) == VK_SUCCESS) {
        m->exportHandle = (void*)h;
        gpu_log("vulkan: memimg 导出 NT 句柄 %p (%dx%d 零拷贝)", h, m->w, m->h);
    } else {
        gpu_log("vulkan: vkGetMemoryWin32HandleKHR 失败");
    }
#else
    if (m->exportFd >= 0) return;
    VkMemoryGetFdInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    gi.memory = m->mem;
    gi.handleType = SC_VK_EXTMEM_HANDLE_TYPE;
    int fd = -1;
    if (g_vk.getMemFd(g_vk.dev.device, &gi, &fd) == VK_SUCCESS) {
        m->exportFd = fd;
        gpu_log("vulkan: memimg 导出 dma-buf fd %d (%dx%d 零拷贝)", fd, m->w, m->h);
    } else {
        gpu_log("vulkan: vkGetMemoryFdKHR 失败");
    }
#endif
}

static bool vkMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    VkMemimg* m = (VkMemimg*)img->backend;
    if (!m) return false;
    if (with_fence) vkDeviceWaitIdle(g_vk.dev.device);   /* 无导出 fence → CPU 同步 */
    memimg_ensure_export(m);
    out->planes = 1;
    out->stride[0] = (uint32_t)(m->w * 4);
    out->offset[0] = 0;
    out->fourcc = img->desc.fourcc;
    out->width = m->w;
    out->height = m->h;
    out->sync_fd = -1;
#if P_WIN
    out->fd[0] = -1;
    out->native = m->exportHandle;   /* NT 句柄（可导入 D3D / 送编码器）；无扩展则 NULL */
#else
    out->fd[0] = m->exportFd;         /* dma-buf fd（可导入 GL/v4l2）；无扩展则 -1 */
    out->native = NULL;
#endif
    return true;
}

static void* vkMemimgNative(gpu_memimg_t* img) {
    VkMemimg* m = (VkMemimg*)img->backend;
    return m ? (void*)(uintptr_t)m->image : NULL;   /* gfx Mode B 借此 VkImage 建 view */
}

static void* vkMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    VkMemimg* m = (VkMemimg*)img->backend;
    if (!m) return NULL;
    VkDevice dev = g_vk.dev.device;
    size_t sz = (size_t)m->w * (size_t)m->h * 4;
    if (!m->staging) {
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = sz ? sz : 1;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &bi, NULL, &m->staging) != VK_SUCCESS) return NULL;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, m->staging, &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(dev, &mai, NULL, &m->stagingMem) != VK_SUCCESS) {
            vkDestroyBuffer(dev, m->staging, NULL); m->staging = VK_NULL_HANDLE; return NULL;
        }
        vkBindBufferMemory(dev, m->staging, m->stagingMem, 0);
        vkMapMemory(dev, m->stagingMem, 0, VK_WHOLE_SIZE, 0, &m->mapped);
    }
    /* image(TRANSFER_SRC_OPTIMAL) → staging buffer 拷贝，紧密排布 */
    VkCommandBuffer cmd = vk_oneshot_begin();
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)m->w;
    region.imageExtent.height = (uint32_t)m->h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, m->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m->staging, 1, &region);
    vk_oneshot_end(cmd);
    if (out_stride) *out_stride = (uint32_t)(m->w * 4);
    return m->mapped;
}

static void vkMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)img; (void)plane;   /* staging 随 memimg 生命期，unmap 空操作 */
}

static void vkMemimgFree(gpu_memimg_t* img) {
    VkMemimg* m = (VkMemimg*)img->backend;
    if (!m) return;
    VkDevice dev = g_vk.dev.device;
#if P_WIN
    if (m->exportHandle) CloseHandle((HANDLE)m->exportHandle);
#else
    if (m->exportFd >= 0) close(m->exportFd);
#endif
    if (m->mapped)     vkUnmapMemory(dev, m->stagingMem);
    if (m->staging)    vkDestroyBuffer(dev, m->staging, NULL);
    if (m->stagingMem) vkFreeMemory(dev, m->stagingMem, NULL);
    if (m->view)       vkDestroyImageView(dev, m->view, NULL);
    if (m->image)      vkDestroyImage(dev, m->image, NULL);
    if (m->mem)        vkFreeMemory(dev, m->mem, NULL);
    free(m);
    img->backend = NULL;
}

/* MEMORY surface：无交换链/表面/信号量，仅共享深度 + 在飞栅栏；
 * memimg 环由公共层预分配（surf->ring_imgs），本函数按需建深度与同步。 */
static bool vkMemorySurfaceCreate(gpu_surface_t* surf) {
    VkDevice d = g_vk.dev.device;
    VkSurfaceCtx* c = (VkSurfaceCtx*)calloc(1, sizeof(VkSurfaceCtx));
    if (!c) return false;
    c->memory = true;
    c->extent.width  = (uint32_t)surf->desc.width;
    c->extent.height = (uint32_t)surf->desc.height;
    c->colorFormat = vk_color_format(surf->desc.color_format);
    c->imageCount = (uint32_t)surf->desc.image_count;

    c->depthFormat = pick_depth_format(surf->desc.depth_format);
    c->hasDepth = (c->depthFormat != VK_FORMAT_UNDEFINED);
    if (c->hasDepth) {
        VkImageCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = c->depthFormat;
        ic.extent.width = c->extent.width;
        ic.extent.height = c->extent.height;
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

    /* 在飞栅栏（SIGNALED：首次 wait 直接通过） */
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < VK_INFLIGHT; i++)
        VKCK(vkCreateFence(d, &fci, NULL, &c->inFlight[i]));
    c->frameIndex = 0;
    c->acquired = false;

    surf->backend = c;
    if (!g_vk.cur) g_vk.cur = c;
    gpu_log("vulkan: MEMORY surface 就绪 (%ux%u, %u 镜像环)",
            c->extent.width, c->extent.height, c->imageCount);
    return true;
}

static bool vkSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!img) return false;
    return vkMemimgExport(img, out, false);   /* commit 后 demo 已 gfx_finish */
}

/* ============================================================
 * surface 生命周期
 * ============================================================ */

static bool vkSurfaceCreate(gpu_surface_t* surf) {
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY)
        return vkMemorySurfaceCreate(surf);
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
    if (!c) return false;
    g_vk.cur = c;
    VkDevice d = g_vk.dev.device;

    if (c->memory) {
        if (surf->ring_cur < 0) return false;
        uint32_t mfi = c->frameIndex;
        vkWaitForFences(d, 1, &c->inFlight[mfi], VK_TRUE, UINT64_MAX);
        vkResetFences(d, 1, &c->inFlight[mfi]);
        gpu_memimg_t* mi = gpu_lookup_memimg(surf->ring_imgs[surf->ring_cur]);
        VkMemimg* m = mi ? (VkMemimg*)mi->backend : NULL;
        if (!m) return false;
        memset(out, 0, sizeof(*out));
        out->color = (void*)(uintptr_t)m->view;
        out->depth = c->hasDepth ? (void*)(uintptr_t)c->depthView : NULL;
        out->width = m->w;
        out->height = m->h;
        out->sample_count = 1;
        out->color_format = surf->desc.color_format;
        out->depth_format = c->hasDepth ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;
        c->acquired = true;
        return true;
    }
    if (!c->swapchain) return false;

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

    if (c->memory) {
        /* 无表面：无 present，仅推进在飞帧槽（环推进由公共层负责） */
        c->frameIndex = (c->frameIndex + 1) % VK_INFLIGHT;
        c->acquired = false;
        return;
    }
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
    if (c && c->memory) {
        /* 无表面：不带交换链信号量，仅在飞栅栏供 gfx 提交与回读同步 */
        if (out_wait)   *out_wait   = VK_NULL_HANDLE;
        if (out_signal) *out_signal = VK_NULL_HANDLE;
        if (out_fence)  *out_fence  = c->inFlight[c->frameIndex];
    } else if (c) {
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

int sc_gpu_vk_current_is_memory(void) {
    return (g_vk.cur && g_vk.cur->memory) ? 1 : 0;
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
    .memimg_alloc = vkMemimgAlloc,
    .memimg_import = vkMemimgImport,
    .memimg_export = vkMemimgExport,
    .memimg_native = vkMemimgNative,
    .memimg_map = vkMemimgMap,
    .memimg_unmap = vkMemimgUnmap,
    .memimg_free = vkMemimgFree,
    .surface_dequeue = vkSurfaceDequeue,
};

const gpu_env_api* gpu_env_vulkan(void) { return &vulkanApi; }

#endif /* SC_GPU_VULKAN */
