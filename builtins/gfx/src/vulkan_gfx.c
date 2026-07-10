/* ============================================================
 * vulkan_gfx.c —— Vulkan 渲染后端（gfx 层）
 * ============================================================
 * 定位：gfx_backend_api 的 Vulkan 实现——消费 SPIR-V + 反射清单，
 *   建资源/管线，录制命令缓冲，交换链 pass 绘制并提交呈现。
 *
 * 与 env 的契约（gpu_vk.h）：
 *   · sc_gpu_device() 取设备聚合体（instance/phys/device/queue/family）
 *   · 交换链 pass：begin_pass 内 sc_gpu_frame_acquire() 取镜像 view；
 *     commit 提交命令缓冲（等 imgAvail、发 renderDone、栅栏 inFlight），
 *     再 sc_gpu_frame_end() 呈现
 *   · 渲染通道格式取自 sc_gpu_vk_color_format()/depth_format()
 *
 * 覆盖范围：交换链图形 pass（三角形闭环）、离屏 pass（MEMORY surface 环 +
 *   memimg 绑定，finalLayout=TRANSFER_SRC 供回读）、顶点/索引缓冲、uniform 块
 *   与纹理采样（描述符集，反射 set/binding 直映）。计算 dispatch 为最小实现/占位。
 *   非 Vulkan 目标经 SC_GPU_VULKAN 守卫空化（Windows/Linux 启用）。
 * ============================================================ */

#include "internal.h"

#ifdef SC_GPU_VULKAN

#include "../../gpu/gpu_vk.h"
#include <stdlib.h>
#include <string.h>

#define GVK_INFLIGHT 2
#define GVK_UBO_ARENA (256 * 1024)   /* 每帧 uniform 竞技场字节 */

#define GVKCK(expr) do { \
    VkResult gvk__r = (expr); \
    if (gvk__r != VK_SUCCESS) gfx_log("vulkan-gfx: %s 失败 (VkResult=%d)", #expr, (int)gvk__r); \
} while (0)

/* ---- 后端私有资源体 --------------------------------------- */
typedef struct {
    VkBuffer       buf;
    VkDeviceMemory mem;
    bool           hostVisible;
    void*          mapped;
    size_t         size;
} GvkBuffer;

typedef struct {
    VkImage        image;
    VkDeviceMemory mem;
    VkImageView    view;
    VkFormat       format;
    int            width, height;
    bool           borrowed;   /* memimg 绑定：image/mem 借自 gpu env，仅 own view */
} GvkImage;

typedef struct {
    VkSampler sampler;
} GvkSampler;

typedef struct {
    VkShaderModule vs, fs, cs;
    char           vsEntry[64], fsEntry[64], csEntry[64];
} GvkShader;

typedef struct {
    VkPipeline            pipe;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout setLayout;   /* VK_NULL_HANDLE 若无绑定 */
    bool                  hasSet;
    bool                  compute;
    VkIndexType           idxType;
    bool                  indexed;
} GvkPipeline;

/* ---- 全局状态 --------------------------------------------- */
typedef struct {
    bool               valid;
    sc_gpu_vk_device*  dev;
    VkCommandPool      cmdPool;
    VkCommandBuffer    cmd[GVK_INFLIGHT];
    VkDescriptorPool   descPool;

    /* 交换链兼容渲染通道（懒建，管线与 framebuffer 共用） */
    VkRenderPass       swapPass;
    VkFormat           swapColor;
    VkFormat           swapDepth;
    bool               swapHasDepth;

    /* 离屏渲染通道缓存（memimg 目标：finalLayout=TRANSFER_SRC 供回读） */
    struct { VkRenderPass rp; VkFormat color; bool hasDepth; } offCache[8];
    int                offCount;

    /* framebuffer 缓存（按颜色 view 键） */
    struct { VkImageView key; VkFramebuffer fb; VkImageView depthKey; } fbCache[16];
    int                fbCount;

    /* 每帧 uniform 竞技场 */
    GvkBuffer          ubo[GVK_INFLIGHT];
    uint32_t           uboOffset;

    /* 帧内状态 */
    uint32_t           frameIndex;
    VkCommandBuffer    curCmd;
    bool               inPass;
    bool               inSwapPass;
    VkExtent2D         curExtent;
    GvkPipeline*       curPipe;

    /* 本帧同步（begin_pass 取自 env） */
    VkSemaphore        waitSem, signalSem;
    VkFence            inFlight;

    /* 待写描述符（apply_uniforms/apply_bindings 累积，draw 前刷新） */
    VkDescriptorBufferInfo uboInfo[SC_GFX_MAX_UNIFORM_BLOCKS * 2];
    bool                   uboSet[SC_GFX_MAX_UNIFORM_BLOCKS * 2];
    VkDescriptorImageInfo  imgInfo[SC_GFX_MAX_IMAGES];
    bool                   imgSet[SC_GFX_MAX_IMAGES];
    bool                   descDirty;
} GfxVk;

static GfxVk g;

/* ============================================================
 * 格式 / 状态映射
 * ============================================================ */

static VkFormat gvk_pixfmt(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:       return VK_FORMAT_R8_UNORM;
        case SC_GPU_PIXELFORMAT_RG8:      return VK_FORMAT_R8G8_UNORM;
        case SC_GPU_PIXELFORMAT_RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case SC_GPU_PIXELFORMAT_SRGB8A8:  return VK_FORMAT_R8G8B8A8_SRGB;
        case SC_GPU_PIXELFORMAT_BGRA8:    return VK_FORMAT_B8G8R8A8_UNORM;
        case SC_GPU_PIXELFORMAT_RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SC_GPU_PIXELFORMAT_RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SC_GPU_PIXELFORMAT_R32F:     return VK_FORMAT_R32_SFLOAT;
        case SC_GPU_PIXELFORMAT_RG32F:    return VK_FORMAT_R32G32_SFLOAT;
        default:                          return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

static VkFormat gvk_vertfmt(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:   return VK_FORMAT_R32_SFLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT2:  return VK_FORMAT_R32G32_SFLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT3:  return VK_FORMAT_R32G32B32_SFLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SC_GFX_VERTEXFORMAT_BYTE4:   return VK_FORMAT_R8G8B8A8_SINT;
        case SC_GFX_VERTEXFORMAT_BYTE4N:  return VK_FORMAT_R8G8B8A8_SNORM;
        case SC_GFX_VERTEXFORMAT_UBYTE4:  return VK_FORMAT_R8G8B8A8_UINT;
        case SC_GFX_VERTEXFORMAT_UBYTE4N: return VK_FORMAT_R8G8B8A8_UNORM;
        case SC_GFX_VERTEXFORMAT_SHORT2:  return VK_FORMAT_R16G16_SINT;
        case SC_GFX_VERTEXFORMAT_SHORT2N: return VK_FORMAT_R16G16_SNORM;
        case SC_GFX_VERTEXFORMAT_SHORT4:  return VK_FORMAT_R16G16B16A16_SINT;
        case SC_GFX_VERTEXFORMAT_SHORT4N: return VK_FORMAT_R16G16B16A16_SNORM;
        case SC_GFX_VERTEXFORMAT_HALF2:   return VK_FORMAT_R16G16_SFLOAT;
        case SC_GFX_VERTEXFORMAT_HALF4:   return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SC_GFX_VERTEXFORMAT_UINT:    return VK_FORMAT_R32_UINT;
        default:                          return VK_FORMAT_R32G32B32_SFLOAT;
    }
}

static int gvk_vertfmt_size(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:   return 4;
        case SC_GFX_VERTEXFORMAT_FLOAT2:  return 8;
        case SC_GFX_VERTEXFORMAT_FLOAT3:  return 12;
        case SC_GFX_VERTEXFORMAT_FLOAT4:  return 16;
        case SC_GFX_VERTEXFORMAT_BYTE4:
        case SC_GFX_VERTEXFORMAT_BYTE4N:
        case SC_GFX_VERTEXFORMAT_UBYTE4:
        case SC_GFX_VERTEXFORMAT_UBYTE4N:
        case SC_GFX_VERTEXFORMAT_SHORT2:
        case SC_GFX_VERTEXFORMAT_SHORT2N:
        case SC_GFX_VERTEXFORMAT_HALF2:
        case SC_GFX_VERTEXFORMAT_UINT:    return 4;
        case SC_GFX_VERTEXFORMAT_SHORT4:
        case SC_GFX_VERTEXFORMAT_SHORT4N:
        case SC_GFX_VERTEXFORMAT_HALF4:   return 8;
        default:                          return 12;
    }
}

static VkPrimitiveTopology gvk_topo(sc_gfx_primitive p) {
    switch (p) {
        case SC_GFX_PRIMITIVE_POINTS:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case SC_GFX_PRIMITIVE_LINES:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case SC_GFX_PRIMITIVE_LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case SC_GFX_PRIMITIVE_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default:                              return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkCullModeFlags gvk_cull(sc_gfx_cull c) {
    switch (c) {
        case SC_GFX_CULL_FRONT: return VK_CULL_MODE_FRONT_BIT;
        case SC_GFX_CULL_BACK:  return VK_CULL_MODE_BACK_BIT;
        default:                return VK_CULL_MODE_NONE;
    }
}

static VkCompareOp gvk_compare(sc_gfx_compare c) {
    switch (c) {
        case SC_GFX_COMPARE_NEVER:         return VK_COMPARE_OP_NEVER;
        case SC_GFX_COMPARE_LESS:          return VK_COMPARE_OP_LESS;
        case SC_GFX_COMPARE_EQUAL:         return VK_COMPARE_OP_EQUAL;
        case SC_GFX_COMPARE_LESS_EQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case SC_GFX_COMPARE_GREATER:       return VK_COMPARE_OP_GREATER;
        case SC_GFX_COMPARE_NOT_EQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
        case SC_GFX_COMPARE_GREATER_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        default:                           return VK_COMPARE_OP_ALWAYS;
    }
}

static VkBlendFactor gvk_blend(sc_gfx_blend_factor f) {
    switch (f) {
        case SC_GFX_BLEND_ZERO:                     return VK_BLEND_FACTOR_ZERO;
        case SC_GFX_BLEND_ONE:                      return VK_BLEND_FACTOR_ONE;
        case SC_GFX_BLEND_SRC_COLOR:                return VK_BLEND_FACTOR_SRC_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_SRC_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case SC_GFX_BLEND_SRC_ALPHA:                return VK_BLEND_FACTOR_SRC_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_SRC_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case SC_GFX_BLEND_DST_COLOR:                return VK_BLEND_FACTOR_DST_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_DST_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case SC_GFX_BLEND_DST_ALPHA:                return VK_BLEND_FACTOR_DST_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_DST_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case SC_GFX_BLEND_SRC_ALPHA_SATURATED:      return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case SC_GFX_BLEND_BLEND_COLOR:              return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_BLEND_COLOR:    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        default:                                    return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp gvk_blendop(sc_gfx_blend_op o) {
    switch (o) {
        case SC_GFX_BLENDOP_SUBTRACT:         return VK_BLEND_OP_SUBTRACT;
        case SC_GFX_BLENDOP_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case SC_GFX_BLENDOP_MIN:              return VK_BLEND_OP_MIN;
        case SC_GFX_BLENDOP_MAX:              return VK_BLEND_OP_MAX;
        default:                              return VK_BLEND_OP_ADD;
    }
}

static uint32_t gvk_find_mem(uint32_t bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g.dev->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0;
}

static bool gvk_make_buffer(GvkBuffer* b, size_t size, VkBufferUsageFlags usage, bool hostVisible) {
    memset(b, 0, sizeof(*b));
    b->size = size;
    b->hostVisible = hostVisible;
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size ? size : 1;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g.dev->device, &bi, NULL, &b->buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g.dev->device, b->buf, &mr);
    VkMemoryPropertyFlags props = hostVisible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = gvk_find_mem(mr.memoryTypeBits, props);
    if (vkAllocateMemory(g.dev->device, &mai, NULL, &b->mem) != VK_SUCCESS) {
        vkDestroyBuffer(g.dev->device, b->buf, NULL);
        b->buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(g.dev->device, b->buf, b->mem, 0);
    if (hostVisible) vkMapMemory(g.dev->device, b->mem, 0, VK_WHOLE_SIZE, 0, &b->mapped);
    return true;
}

static void gvk_free_buffer(GvkBuffer* b) {
    if (!b) return;
    if (b->mapped) { vkUnmapMemory(g.dev->device, b->mem); b->mapped = NULL; }
    if (b->buf) vkDestroyBuffer(g.dev->device, b->buf, NULL);
    if (b->mem) vkFreeMemory(g.dev->device, b->mem, NULL);
    memset(b, 0, sizeof(*b));
}

/* ============================================================
 * 生命周期
 * ============================================================ */

static bool gvkInit(const sc_gfx_desc* desc) {
    (void)desc;
    memset(&g, 0, sizeof(g));
    g.dev = (sc_gpu_vk_device*)sc_gpu_device();
    if (!g.dev || !g.dev->device) { gfx_log("vulkan-gfx: 无 Vulkan 设备"); return false; }

    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g.dev->queue_family;
    if (vkCreateCommandPool(g.dev->device, &pci, NULL, &g.cmdPool) != VK_SUCCESS) {
        gfx_log("vulkan-gfx: 命令池创建失败");
        return false;
    }

    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = g.cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = GVK_INFLIGHT;
    GVKCK(vkAllocateCommandBuffers(g.dev->device, &cai, g.cmd));

    /* 描述符池（uniform buffer + combined sampler，够小规模用） */
    VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 },
    };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 512;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = sizes;
    GVKCK(vkCreateDescriptorPool(g.dev->device, &dpci, NULL, &g.descPool));

    for (int i = 0; i < GVK_INFLIGHT; i++)
        gvk_make_buffer(&g.ubo[i], GVK_UBO_ARENA, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);

    g.valid = true;
    return true;
}

static void gvkShutdown(void) {
    if (!g.valid) return;
    VkDevice d = g.dev->device;
    vkDeviceWaitIdle(d);
    for (int i = 0; i < g.fbCount; i++)
        if (g.fbCache[i].fb) vkDestroyFramebuffer(d, g.fbCache[i].fb, NULL);
    if (g.swapPass) vkDestroyRenderPass(d, g.swapPass, NULL);
    for (int i = 0; i < g.offCount; i++)
        if (g.offCache[i].rp) vkDestroyRenderPass(d, g.offCache[i].rp, NULL);
    for (int i = 0; i < GVK_INFLIGHT; i++) gvk_free_buffer(&g.ubo[i]);
    if (g.descPool) vkDestroyDescriptorPool(d, g.descPool, NULL);
    if (g.cmdPool)  vkDestroyCommandPool(d, g.cmdPool, NULL);
    memset(&g, 0, sizeof(g));
}

static void gvkFinish(void) {
    if (g.valid) vkDeviceWaitIdle(g.dev->device);
}

/* ============================================================
 * 交换链渲染通道（懒建，供管线与 framebuffer 共用）
 * ============================================================ */

static VkRenderPass ensure_swap_pass(void) {
    if (g.swapPass) return g.swapPass;
    g.swapColor = sc_gpu_vk_color_format();
    g.swapDepth = sc_gpu_vk_depth_format();
    g.swapHasDepth = (g.swapDepth != VK_FORMAT_UNDEFINED);
    if (g.swapColor == VK_FORMAT_UNDEFINED) g.swapColor = VK_FORMAT_B8G8R8A8_UNORM;

    VkAttachmentDescription att[2];
    memset(att, 0, sizeof(att));
    att[0].format = g.swapColor;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub = { 0 };
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    uint32_t nAtt = 1;
    if (g.swapHasDepth) {
        att[1].format = g.swapDepth;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pDepthStencilAttachment = &depthRef;
        nAtt = 2;
    }

    VkSubpassDependency dep = { 0 };
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = nAtt;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    GVKCK(vkCreateRenderPass(g.dev->device, &rpci, NULL, &g.swapPass));
    return g.swapPass;
}

static VkFramebuffer ensure_framebuffer(VkRenderPass rp, VkImageView colorView, VkImageView depthView, VkExtent2D ext) {
    for (int i = 0; i < g.fbCount; i++)
        if (g.fbCache[i].key == colorView && g.fbCache[i].depthKey == depthView)
            return g.fbCache[i].fb;

    VkImageView atts[2];
    uint32_t n = 0;
    atts[n++] = colorView;
    if (depthView) atts[n++] = depthView;

    VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = rp;
    fci.attachmentCount = n;
    fci.pAttachments = atts;
    fci.width = ext.width;
    fci.height = ext.height;
    fci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    GVKCK(vkCreateFramebuffer(g.dev->device, &fci, NULL, &fb));
    if (g.fbCount < 16) {
        g.fbCache[g.fbCount].key = colorView;
        g.fbCache[g.fbCount].depthKey = depthView;
        g.fbCache[g.fbCount].fb = fb;
        g.fbCount++;
    }
    return fb;
}

/* 离屏渲染通道（memimg 目标）：finalLayout=TRANSFER_SRC_OPTIMAL 供 map 拷贝回读。
 * 兼容性只需附件格式/采样匹配——与交换链通道同格式的管线可跨用（Mode A）。 */
static VkRenderPass ensure_offscreen_pass(VkFormat color, bool hasDepth) {
    for (int i = 0; i < g.offCount; i++)
        if (g.offCache[i].color == color && g.offCache[i].hasDepth == hasDepth)
            return g.offCache[i].rp;

    VkAttachmentDescription att[2];
    memset(att, 0, sizeof(att));
    att[0].format = color;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0 };
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    uint32_t nAtt = 1;
    if (hasDepth) {
        att[1].format = sc_gpu_vk_depth_format();
        if (att[1].format == VK_FORMAT_UNDEFINED) att[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pDepthStencilAttachment = &depthRef;
        nAtt = 2;
    }

    /* 结束时颜色写完 → 供后续 transfer（memimg image→buffer 拷贝）读取 */
    VkSubpassDependency dep[2];
    memset(dep, 0, sizeof(dep));
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep[0].srcAccessMask = 0;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = nAtt;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = dep;
    VkRenderPass rp = VK_NULL_HANDLE;
    GVKCK(vkCreateRenderPass(g.dev->device, &rpci, NULL, &rp));
    if (g.offCount < 8) {
        g.offCache[g.offCount].rp = rp;
        g.offCache[g.offCount].color = color;
        g.offCache[g.offCount].hasDepth = hasDepth;
        g.offCount++;
    }
    return rp;
}

/* 交换链尺寸变化时缓存的 framebuffer 失效（image view 被销毁） */
static void invalidate_framebuffers(void) {
    for (int i = 0; i < g.fbCount; i++)
        if (g.fbCache[i].fb) vkDestroyFramebuffer(g.dev->device, g.fbCache[i].fb, NULL);
    g.fbCount = 0;
}

/* ============================================================
 * 资源：buffer / image / sampler
 * ============================================================ */

static bool gvkBufferCreate(gfx_buffer_t* buf) {
    GvkBuffer* b = (GvkBuffer*)calloc(1, sizeof(GvkBuffer));
    if (!b) return false;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    switch (buf->desc.kind) {
        case SC_GFX_BUFFERKIND_INDEX:   usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
        case SC_GFX_BUFFERKIND_STORAGE: usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
        default:                        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
    }
    size_t sz = buf->desc.size ? buf->desc.size : buf->desc.data.size;
    /* host-visible 便于 update（薄层，不做 staging 优化） */
    if (!gvk_make_buffer(b, sz, usage, true)) { free(b); return false; }
    if (buf->desc.data.ptr && buf->desc.data.size && b->mapped)
        memcpy(b->mapped, buf->desc.data.ptr, buf->desc.data.size);
    buf->backend = b;
    return true;
}

static void gvkBufferDestroy(gfx_buffer_t* buf) {
    GvkBuffer* b = (GvkBuffer*)buf->backend;
    if (!b) return;
    gvk_free_buffer(b);
    free(b);
    buf->backend = NULL;
}

static void gvkBufferUpdate(gfx_buffer_t* buf, const sc_gfx_range* data, int offset) {
    GvkBuffer* b = (GvkBuffer*)buf->backend;
    if (!b || !b->mapped || !data || !data->ptr) return;
    if ((size_t)offset + data->size > b->size) return;
    memcpy((char*)b->mapped + offset, data->ptr, data->size);
}

static bool gvkImageCreate(gfx_image_t* img) {
    GvkImage* im = (GvkImage*)calloc(1, sizeof(GvkImage));
    if (!im) return false;
    VkDevice d = g.dev->device;
    im->width = img->desc.width;
    im->height = img->desc.height;
    im->format = gvk_pixfmt(img->desc.format ? img->desc.format : SC_GPU_PIXELFORMAT_RGBA8);

    /* Mode B：绑定 gpu env 的 memimg VkImage，仅建自有 view（不 own image/mem） */
    if (img->desc.memimg) {
        im->image = (VkImage)(uintptr_t)sc_gpu_memimg_native(img->desc.memimg);
        if (!im->image) {
            gfx_log("vulkan-gfx: memimg %u 无效", img->desc.memimg);
            free(im); return false;
        }
        im->borrowed = true;
        VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        iv.image = im->image;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = im->format;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        if (vkCreateImageView(d, &iv, NULL, &im->view) != VK_SUCCESS) { free(im); return false; }
        img->backend = im;
        return true;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (img->desc.render_target) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = im->format;
    ic.extent.width = (uint32_t)(im->width > 0 ? im->width : 1);
    ic.extent.height = (uint32_t)(im->height > 0 ? im->height : 1);
    ic.extent.depth = 1;
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = usage;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(d, &ic, NULL, &im->image) != VK_SUCCESS) { free(im); return false; }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d, im->image, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = gvk_find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(d, &mai, NULL, &im->mem);
    vkBindImageMemory(d, im->image, im->mem, 0);

    VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image = im->image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = im->format;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    vkCreateImageView(d, &iv, NULL, &im->view);

    img->backend = im;
    return true;
}

static void gvkImageDestroy(gfx_image_t* img) {
    GvkImage* im = (GvkImage*)img->backend;
    if (!im) return;
    VkDevice d = g.dev->device;
    if (im->view)  vkDestroyImageView(d, im->view, NULL);
    if (!im->borrowed) {
        if (im->image) vkDestroyImage(d, im->image, NULL);
        if (im->mem)   vkFreeMemory(d, im->mem, NULL);
    }
    free(im);
    img->backend = NULL;
}

static void gvkImageUpdate(gfx_image_t* img, const sc_gfx_image_data* data) {
    (void)img; (void)data;
    /* 薄层：纹理上传（staging + layout 转换）暂略——本阶段聚焦交换链绘制 */
}

static VkFilter gvk_filter(sc_gfx_filter f) {
    return (f == SC_GFX_FILTER_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}
static VkSamplerAddressMode gvk_wrap(sc_gfx_wrap w) {
    switch (w) {
        case SC_GFX_WRAP_CLAMP:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SC_GFX_WRAP_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case SC_GFX_WRAP_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:                 return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static bool gvkSamplerCreate(gfx_sampler_t* smp) {
    GvkSampler* s = (GvkSampler*)calloc(1, sizeof(GvkSampler));
    if (!s) return false;
    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = gvk_filter(smp->desc.mag_filter);
    sci.minFilter = gvk_filter(smp->desc.min_filter);
    sci.mipmapMode = (smp->desc.mipmap_filter == SC_GFX_FILTER_LINEAR)
                     ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = gvk_wrap(smp->desc.wrap_u);
    sci.addressModeV = gvk_wrap(smp->desc.wrap_v);
    sci.addressModeW = gvk_wrap(smp->desc.wrap_w);
    sci.maxLod = VK_LOD_CLAMP_NONE;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(g.dev->device, &sci, NULL, &s->sampler) != VK_SUCCESS) { free(s); return false; }
    smp->backend = s;
    return true;
}

static void gvkSamplerDestroy(gfx_sampler_t* smp) {
    GvkSampler* s = (GvkSampler*)smp->backend;
    if (!s) return;
    if (s->sampler) vkDestroySampler(g.dev->device, s->sampler, NULL);
    free(s);
    smp->backend = NULL;
}

/* ============================================================
 * shader / pipeline
 * ============================================================ */

static VkShaderModule make_module(const sc_gfx_range* code) {
    if (!code || !code->ptr || code->size < 4) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = code->size;
    smci.pCode = (const uint32_t*)code->ptr;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g.dev->device, &smci, NULL, &m) != VK_SUCCESS) {
        gfx_log("vulkan-gfx: vkCreateShaderModule 失败（SPIR-V 无效？）");
        return VK_NULL_HANDLE;
    }
    return m;
}

static void copy_entry(char* dst, const char* src) {
    const char* e = (src && src[0]) ? src : "main";
    size_t n = strlen(e);
    if (n > 62) n = 62;
    memcpy(dst, e, n);
    dst[n] = 0;
}

static bool gvkShaderCreate(gfx_shader_t* shd, const sc_gfx_shader_desc* desc) {
    GvkShader* s = (GvkShader*)calloc(1, sizeof(GvkShader));
    if (!s) return false;
    if (desc->cs.code.ptr) {
        s->cs = make_module(&desc->cs.code);
        copy_entry(s->csEntry, desc->cs.entry);
        if (!s->cs) { free(s); return false; }
    } else {
        s->vs = make_module(&desc->vs.code);
        s->fs = make_module(&desc->fs.code);
        copy_entry(s->vsEntry, desc->vs.entry);
        copy_entry(s->fsEntry, desc->fs.entry);
        if (!s->vs || !s->fs) {
            if (s->vs) vkDestroyShaderModule(g.dev->device, s->vs, NULL);
            if (s->fs) vkDestroyShaderModule(g.dev->device, s->fs, NULL);
            free(s);
            return false;
        }
    }
    /* SPIR-V 入口恒为 main（scc vulkan 目标）；覆盖为 main */
    copy_entry(s->vsEntry, "main");
    copy_entry(s->fsEntry, "main");
    copy_entry(s->csEntry, "main");
    shd->backend = s;
    return true;
}

static void gvkShaderDestroy(gfx_shader_t* shd) {
    GvkShader* s = (GvkShader*)shd->backend;
    if (!s) return;
    VkDevice d = g.dev->device;
    if (s->vs) vkDestroyShaderModule(d, s->vs, NULL);
    if (s->fs) vkDestroyShaderModule(d, s->fs, NULL);
    if (s->cs) vkDestroyShaderModule(d, s->cs, NULL);
    free(s);
    shd->backend = NULL;
}

/* 从反射建描述符集布局（set 0：uniform 块 + sampler） */
static VkDescriptorSetLayout build_set_layout(const gfx_reflect* rf, bool* outHasSet) {
    VkDescriptorSetLayoutBinding binds[SC_GFX_MAX_UNIFORM_BLOCKS * 2 + SC_GFX_MAX_SAMPLERS];
    uint32_t n = 0;
    for (int i = 0; i < rf->block_count && n < (uint32_t)(sizeof(binds)/sizeof(binds[0])); i++) {
        VkDescriptorSetLayoutBinding b = { 0 };
        b.binding = (uint32_t)rf->blocks[i].slot;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                       VK_SHADER_STAGE_COMPUTE_BIT;
        binds[n++] = b;
    }
    for (int i = 0; i < rf->sampler_count && n < (uint32_t)(sizeof(binds)/sizeof(binds[0])); i++) {
        VkDescriptorSetLayoutBinding b = { 0 };
        b.binding = (uint32_t)rf->samplers[i].slot;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                       VK_SHADER_STAGE_COMPUTE_BIT;
        binds[n++] = b;
    }
    *outHasSet = (n > 0);
    if (n == 0) return VK_NULL_HANDLE;
    VkDescriptorSetLayoutCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = n;
    ci.pBindings = binds;
    VkDescriptorSetLayout sl = VK_NULL_HANDLE;
    GVKCK(vkCreateDescriptorSetLayout(g.dev->device, &ci, NULL, &sl));
    return sl;
}

static bool gvkPipelineCreate(gfx_pipeline_t* pip) {
    GvkShader* sh = (GvkShader*)pip->shader->backend;
    if (!sh) return false;
    GvkPipeline* p = (GvkPipeline*)calloc(1, sizeof(GvkPipeline));
    if (!p) return false;
    VkDevice d = g.dev->device;

    p->setLayout = build_set_layout(&pip->shader->reflect, &p->hasSet);

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (p->hasSet) {
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &p->setLayout;
    }
    GVKCK(vkCreatePipelineLayout(d, &plci, NULL, &p->layout));

    if (pip->desc.compute && sh->cs) {
        p->compute = true;
        VkComputePipelineCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = sh->cs;
        cpci.stage.pName = sh->csEntry;
        cpci.layout = p->layout;
        if (vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cpci, NULL, &p->pipe) != VK_SUCCESS) {
            gfx_log("vulkan-gfx: 计算管线创建失败");
            vkDestroyPipelineLayout(d, p->layout, NULL);
            if (p->setLayout) vkDestroyDescriptorSetLayout(d, p->setLayout, NULL);
            free(p);
            return false;
        }
        pip->backend = p;
        return true;
    }

    /* --- 图形管线 --- */
    /* renderpass 选择：无深度（depth.format=NONE，Mode B 离屏）→ 纯颜色离屏通道；
     * 当前 MEMORY surface（Mode A）→ 与 begin_pass 同一离屏通道（保证 renderpass 一致）；
     * 否则窗口 → 交换链呈现通道。 */
    bool pipeHasDepth = (pip->desc.depth.format != SC_GPU_PIXELFORMAT_NONE);
    VkRenderPass rp;
    if (!pipeHasDepth)
        rp = ensure_offscreen_pass(gvk_pixfmt(pip->desc.colors[0].format), false);
    else if (sc_gpu_vk_current_is_memory())
        rp = ensure_offscreen_pass(sc_gpu_vk_color_format(), true);
    else
        rp = ensure_swap_pass();

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = sh->vs;
    stages[0].pName = sh->vsEntry;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = sh->fs;
    stages[1].pName = sh->fsEntry;

    /* 顶点输入 */
    VkVertexInputBindingDescription vbind[SC_GFX_MAX_VERTEX_BUFFERS];
    VkVertexInputAttributeDescription vattr[SC_GFX_MAX_VERTEX_ATTRS];
    uint32_t nBind = 0, nAttr = 0;
    int autoStride[SC_GFX_MAX_VERTEX_BUFFERS];
    memset(autoStride, 0, sizeof(autoStride));
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++) {
        sc_gfx_vertex_format vf = pip->desc.attrs[i].format;
        if (vf == SC_GFX_VERTEXFORMAT_INVALID) continue;
        int bi = pip->desc.attrs[i].buffer_index;
        int off = pip->desc.attrs[i].offset;
        if (off == 0 && nAttr > 0) off = autoStride[bi];
        vattr[nAttr].location = (uint32_t)i;
        vattr[nAttr].binding = (uint32_t)bi;
        vattr[nAttr].format = gvk_vertfmt(vf);
        vattr[nAttr].offset = (uint32_t)off;
        autoStride[bi] = off + gvk_vertfmt_size(vf);
        nAttr++;
    }
    for (int b = 0; b < SC_GFX_MAX_VERTEX_BUFFERS; b++) {
        bool used = false;
        for (uint32_t a = 0; a < nAttr; a++) if (vattr[a].binding == (uint32_t)b) { used = true; break; }
        if (!used) continue;
        int stride = pip->desc.buffers[b].stride;
        if (stride == 0) stride = autoStride[b];
        vbind[nBind].binding = (uint32_t)b;
        vbind[nBind].stride = (uint32_t)stride;
        vbind[nBind].inputRate = pip->desc.buffers[b].step_per_instance
            ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        nBind++;
    }

    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = nBind;
    vi.pVertexBindingDescriptions = nBind ? vbind : NULL;
    vi.vertexAttributeDescriptionCount = nAttr;
    vi.pVertexAttributeDescriptions = nAttr ? vattr : NULL;

    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = gvk_topo(pip->desc.primitive);

    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = gvk_cull(pip->desc.cull);
    rs.frontFace = (pip->desc.winding == SC_GFX_WINDING_CW)
        ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    if (pip->desc.depth.bias != 0.0f || pip->desc.depth.bias_slope_scale != 0.0f) {
        rs.depthBiasEnable = VK_TRUE;
        rs.depthBiasConstantFactor = pip->desc.depth.bias;
        rs.depthBiasSlopeFactor = pip->desc.depth.bias_slope_scale;
        rs.depthBiasClamp = pip->desc.depth.bias_clamp;
    }

    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    if (g.swapHasDepth) {
        bool wantDepth = (pip->desc.depth.compare != SC_GFX_COMPARE_DEFAULT &&
                          pip->desc.depth.compare != SC_GFX_COMPARE_ALWAYS) ||
                         pip->desc.depth.write_enabled;
        ds.depthTestEnable = wantDepth ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = pip->desc.depth.write_enabled ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = gvk_compare(pip->desc.depth.compare);
    }

    /* 混合（单颜色目标） */
    VkPipelineColorBlendAttachmentState cba = { 0 };
    const sc_gfx_blend_state* bs = &pip->desc.colors[0].blend;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (bs->enabled) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = gvk_blend(bs->src_factor_rgb);
        cba.dstColorBlendFactor = gvk_blend(bs->dst_factor_rgb);
        cba.colorBlendOp = gvk_blendop(bs->op_rgb);
        cba.srcAlphaBlendFactor = gvk_blend(bs->src_factor_alpha);
        cba.dstAlphaBlendFactor = gvk_blend(bs->dst_factor_alpha);
        cba.alphaBlendOp = gvk_blendop(bs->op_alpha);
    }
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyns[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = p->layout;
    gpci.renderPass = rp;
    gpci.subpass = 0;

    if (vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gpci, NULL, &p->pipe) != VK_SUCCESS) {
        gfx_log("vulkan-gfx: 图形管线创建失败");
        vkDestroyPipelineLayout(d, p->layout, NULL);
        if (p->setLayout) vkDestroyDescriptorSetLayout(d, p->setLayout, NULL);
        free(p);
        return false;
    }

    p->idxType = (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT32)
        ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    p->indexed = (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT16 ||
                  pip->desc.index_type == SC_GFX_INDEXTYPE_UINT32);
    pip->backend = p;
    return true;
}

static void gvkPipelineDestroy(gfx_pipeline_t* pip) {
    GvkPipeline* p = (GvkPipeline*)pip->backend;
    if (!p) return;
    VkDevice d = g.dev->device;
    if (p->pipe) vkDestroyPipeline(d, p->pipe, NULL);
    if (p->layout) vkDestroyPipelineLayout(d, p->layout, NULL);
    if (p->setLayout) vkDestroyDescriptorSetLayout(d, p->setLayout, NULL);
    free(p);
    pip->backend = NULL;
}

/* ============================================================
 * 帧：pass / draw / commit
 * ============================================================ */

static void gvkBeginPass(const sc_gfx_pass* pass, gfx_image_t* colors[], int color_count,
                         gfx_image_t* resolve[], gfx_image_t* depth) {
    (void)resolve;
    if (pass->compute) {
        gfx_log("vulkan-gfx: 计算 pass 暂不支持");
        return;
    }

    VkImageView colorView, depthView;
    VkFormat colorFmt;
    bool hasDepth;
    bool present;   /* true=交换链呈现（信号量 + PRESENT_SRC）；false=离屏（TRANSFER_SRC 供回读） */

    if (color_count > 0) {
        /* Mode B：显式离屏 color 附件（memimg 绑定的 gfx image） */
        GvkImage* ci = (GvkImage*)colors[0]->backend;
        if (!ci) { gfx_log("vulkan-gfx: 离屏 color 无效"); return; }
        GvkImage* di = depth ? (GvkImage*)depth->backend : NULL;
        colorView = ci->view;
        colorFmt  = ci->format;
        depthView = di ? di->view : VK_NULL_HANDLE;
        hasDepth  = (depthView != VK_NULL_HANDLE);
        g.curExtent.width  = (uint32_t)ci->width;
        g.curExtent.height = (uint32_t)ci->height;
        present = false;
        sc_gpu_vk_current_sync(&g.waitSem, &g.signalSem, &g.inFlight);
        /* 离屏无 frame_acquire 代劳栅栏——此处等/重置以复用命令缓冲 */
        if (g.inFlight) {
            vkWaitForFences(g.dev->device, 1, &g.inFlight, VK_TRUE, UINT64_MAX);
            vkResetFences(g.dev->device, 1, &g.inFlight);
        }
    } else {
        /* Mode A / 窗口：交换链风格 pass，从 env 取当前帧 view */
        sc_gpu_frame frame;
        if (!sc_gpu_frame_acquire(&frame)) {
            gfx_log("vulkan-gfx: frame_acquire 失败");
            return;
        }
        sc_gpu_vk_current_sync(&g.waitSem, &g.signalSem, &g.inFlight);
        colorView = (VkImageView)(uintptr_t)frame.color;
        depthView = (VkImageView)(uintptr_t)frame.depth;
        colorFmt  = sc_gpu_vk_color_format();
        if (colorFmt == VK_FORMAT_UNDEFINED) colorFmt = VK_FORMAT_B8G8R8A8_UNORM;
        hasDepth  = (depthView != VK_NULL_HANDLE);
        g.curExtent.width  = (uint32_t)frame.width;
        g.curExtent.height = (uint32_t)frame.height;
        present = !sc_gpu_vk_current_is_memory();   /* MEMORY surface → 离屏，不呈现 */
    }

    VkRenderPass rp = present ? ensure_swap_pass()
                              : ensure_offscreen_pass(colorFmt, hasDepth);
    VkFramebuffer fb = ensure_framebuffer(rp, colorView, depthView, g.curExtent);

    VkCommandBuffer cmd = g.cmd[g.frameIndex];
    g.curCmd = cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clears[2];
    memset(clears, 0, sizeof(clears));
    clears[0].color.float32[0] = pass->action.colors[0].clear[0];
    clears[0].color.float32[1] = pass->action.colors[0].clear[1];
    clears[0].color.float32[2] = pass->action.colors[0].clear[2];
    clears[0].color.float32[3] = pass->action.colors[0].clear[3];
    uint32_t nClear = 1;
    if (hasDepth) {
        float cd = pass->action.depth.clear_depth;
        clears[1].depthStencil.depth = (cd == 0.0f) ? 1.0f : cd;
        clears[1].depthStencil.stencil = pass->action.depth.clear_stencil;
        nClear = 2;
    }

    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = rp;
    rbi.framebuffer = fb;
    rbi.renderArea.extent = g.curExtent;
    rbi.clearValueCount = nClear;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    /* 默认全幅视口/裁剪（Vulkan y 向下） */
    VkViewport vpt = { 0, 0, (float)g.curExtent.width, (float)g.curExtent.height, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, g.curExtent };
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    g.inPass = true;
    g.inSwapPass = present;
    g.curPipe = NULL;
    g.uboOffset = 0;
    memset(g.uboSet, 0, sizeof(g.uboSet));
    memset(g.imgSet, 0, sizeof(g.imgSet));
    g.descDirty = false;
}

static void gvkApplyViewport(int x, int y, int w, int h, bool top_left) {
    (void)top_left;
    if (!g.inPass) return;
    VkViewport vp = { (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f };
    vkCmdSetViewport(g.curCmd, 0, 1, &vp);
}

static void gvkApplyScissor(int x, int y, int w, int h, bool top_left) {
    (void)top_left;
    if (!g.inPass) return;
    VkRect2D sc = { { x, y }, { (uint32_t)w, (uint32_t)h } };
    vkCmdSetScissor(g.curCmd, 0, 1, &sc);
}

static void gvkApplyPipeline(gfx_pipeline_t* pip) {
    GvkPipeline* p = (GvkPipeline*)pip->backend;
    if (!g.inPass || !p) return;
    g.curPipe = p;
    vkCmdBindPipeline(g.curCmd,
        p->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
        p->pipe);
}

static void gvkApplyBindings(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                             gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                             gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                             gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                             gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]) {
    (void)sbufs;
    if (!g.inPass || !g.curPipe) return;
    VkCommandBuffer cmd = g.curCmd;

    /* 顶点缓冲 */
    VkBuffer vb[SC_GFX_MAX_VERTEX_BUFFERS];
    VkDeviceSize vo[SC_GFX_MAX_VERTEX_BUFFERS];
    uint32_t nvb = 0;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++) {
        if (!vbufs[i]) break;
        GvkBuffer* b = (GvkBuffer*)vbufs[i]->backend;
        if (!b) break;
        vb[nvb] = b->buf;
        vo[nvb] = (VkDeviceSize)bnd->vertex_buffer_offsets[i];
        nvb++;
    }
    if (nvb > 0) vkCmdBindVertexBuffers(cmd, 0, nvb, vb, vo);

    /* 索引缓冲 */
    if (ibuf) {
        GvkBuffer* b = (GvkBuffer*)ibuf->backend;
        if (b) vkCmdBindIndexBuffer(cmd, b->buf, (VkDeviceSize)bnd->index_buffer_offset,
                                    g.curPipe->idxType);
    }

    /* 纹理采样（combined image sampler，按反射 sampler.slot） */
    const gfx_reflect* rf = &pip->shader->reflect;
    for (int i = 0; i < rf->sampler_count && i < SC_GFX_MAX_IMAGES; i++) {
        int slot = rf->samplers[i].slot;
        int stage = rf->samplers[i].stage;
        gfx_image_t* im = NULL;
        gfx_sampler_t* sm = NULL;
        int st = (stage == SC_GFX_STAGE_FRAGMENT) ? SC_GFX_STAGE_FRAGMENT
               : (stage == SC_GFX_STAGE_COMPUTE)  ? SC_GFX_STAGE_COMPUTE
               : SC_GFX_STAGE_VERTEX;
        if (imgs && imgs[st]) im = imgs[st][0];
        if (smps && smps[st]) sm = smps[st][0];
        if (im && sm && slot >= 0 && slot < SC_GFX_MAX_IMAGES) {
            GvkImage* gi = (GvkImage*)im->backend;
            GvkSampler* gs = (GvkSampler*)sm->backend;
            if (gi && gs) {
                g.imgInfo[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                g.imgInfo[slot].imageView = gi->view;
                g.imgInfo[slot].sampler = gs->sampler;
                g.imgSet[slot] = true;
                g.descDirty = true;
            }
        }
    }
}

static void gvkApplyUniforms(int stage, int slot, const void* data, size_t size) {
    (void)stage;
    if (!g.inPass || slot < 0 || slot >= SC_GFX_MAX_UNIFORM_BLOCKS * 2) return;
    /* 写入本帧 uniform 竞技场，记录描述符缓冲信息 */
    uint32_t off = (g.uboOffset + 255u) & ~255u;   /* 256 对齐（minUniformBufferOffsetAlignment 上界） */
    if (off + size > GVK_UBO_ARENA) { gfx_log("vulkan-gfx: uniform 竞技场溢出"); return; }
    GvkBuffer* arena = &g.ubo[g.frameIndex];
    if (arena->mapped) memcpy((char*)arena->mapped + off, data, size);
    g.uboInfo[slot].buffer = arena->buf;
    g.uboInfo[slot].offset = off;
    g.uboInfo[slot].range = size;
    g.uboSet[slot] = true;
    g.uboOffset = off + (uint32_t)size;
    g.descDirty = true;
}

/* 刷新描述符集：分配一个临时 set，写入累积的 UBO/纹理，绑定 */
static void flush_descriptors(void) {
    if (!g.descDirty || !g.curPipe || !g.curPipe->hasSet) return;
    VkDevice d = g.dev->device;

    VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = g.descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g.curPipe->setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(d, &ai, &set) != VK_SUCCESS) return;

    VkWriteDescriptorSet writes[SC_GFX_MAX_UNIFORM_BLOCKS * 2 + SC_GFX_MAX_IMAGES];
    uint32_t nw = 0;
    for (int i = 0; i < SC_GFX_MAX_UNIFORM_BLOCKS * 2; i++) {
        if (!g.uboSet[i]) continue;
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = set;
        w.dstBinding = (uint32_t)i;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &g.uboInfo[i];
        writes[nw++] = w;
    }
    for (int i = 0; i < SC_GFX_MAX_IMAGES; i++) {
        if (!g.imgSet[i]) continue;
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = set;
        w.dstBinding = (uint32_t)i;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &g.imgInfo[i];
        writes[nw++] = w;
    }
    if (nw > 0) vkUpdateDescriptorSets(d, nw, writes, 0, NULL);
    vkCmdBindDescriptorSets(g.curCmd,
        g.curPipe->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
        g.curPipe->layout, 0, 1, &set, 0, NULL);
    g.descDirty = false;
}

static void gvkDraw(int base, int count, int instances) {
    if (!g.inPass || !g.curPipe) return;
    if (instances <= 0) instances = 1;
    flush_descriptors();
    if (g.curPipe->indexed)
        vkCmdDrawIndexed(g.curCmd, (uint32_t)count, (uint32_t)instances, (uint32_t)base, 0, 0);
    else
        vkCmdDraw(g.curCmd, (uint32_t)count, (uint32_t)instances, (uint32_t)base, 0);
}

static void gvkDispatch(int gx, int gy, int gz) {
    if (!g.curPipe || !g.curPipe->compute) return;
    flush_descriptors();
    vkCmdDispatch(g.curCmd, (uint32_t)gx, (uint32_t)gy, (uint32_t)gz);
}

static void gvkEndPass(void) {
    if (!g.inPass) return;
    vkCmdEndRenderPass(g.curCmd);   /* 交换链与离屏 pass 均开了 renderpass */
    g.inPass = false;
}

static void gvkCommit(void) {
    if (!g.curCmd) { sc_gpu_frame_end(); return; }
    vkEndCommandBuffer(g.curCmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    if (g.waitSem) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &g.waitSem;
        si.pWaitDstStageMask = &waitStage;
    }
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.curCmd;
    if (g.signalSem) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &g.signalSem;
    }
    GVKCK(vkQueueSubmit(g.dev->queue, 1, &si, g.inFlight));

    /* env 呈现（等 signalSem）并推进其 frameIndex */
    sc_gpu_frame_end();

    g.frameIndex = (g.frameIndex + 1) % GVK_INFLIGHT;
    g.curCmd = VK_NULL_HANDLE;
    g.curPipe = NULL;
    g.waitSem = g.signalSem = VK_NULL_HANDLE;
    g.inFlight = VK_NULL_HANDLE;
}

static void gvkQueryPixelformat(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out) {
    memset(out, 0, sizeof(*out));
    VkFormat vf = gvk_pixfmt(fmt);
    bool isDepth = (fmt == SC_GPU_PIXELFORMAT_DEPTH || fmt == SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
    if (isDepth) vf = (fmt == SC_GPU_PIXELFORMAT_DEPTH) ? VK_FORMAT_D32_SFLOAT
                                                        : VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkFormatProperties fp;
    memset(&fp, 0, sizeof(fp));
    if (g.dev) vkGetPhysicalDeviceFormatProperties(g.dev->phys, vf, &fp);
    VkFormatFeatureFlags feat = fp.optimalTilingFeatures;
    out->sample = (feat & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? 1 : 0;
    out->filter = (feat & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? 1 : 0;
    out->render = (feat & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                           VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) ? 1 : 0;
    out->blend  = (feat & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) ? 1 : 0;
    out->msaa   = out->render;
    out->depth  = isDepth ? 1 : 0;
}

/* ============================================================
 * vtable
 * ============================================================ */

static const gfx_backend_api vulkanApi = {
    .name = "vulkan",
    .init = gvkInit,
    .shutdown = gvkShutdown,
    .finish = gvkFinish,
    .buffer_create = gvkBufferCreate,
    .buffer_destroy = gvkBufferDestroy,
    .buffer_update = gvkBufferUpdate,
    .image_create = gvkImageCreate,
    .image_destroy = gvkImageDestroy,
    .image_update = gvkImageUpdate,
    .sampler_create = gvkSamplerCreate,
    .sampler_destroy = gvkSamplerDestroy,
    .shader_create = gvkShaderCreate,
    .shader_destroy = gvkShaderDestroy,
    .pipeline_create = gvkPipelineCreate,
    .pipeline_destroy = gvkPipelineDestroy,
    .begin_pass = gvkBeginPass,
    .apply_viewport = gvkApplyViewport,
    .apply_scissor = gvkApplyScissor,
    .apply_pipeline = gvkApplyPipeline,
    .apply_bindings = gvkApplyBindings,
    .apply_uniforms = gvkApplyUniforms,
    .draw = gvkDraw,
    .dispatch = gvkDispatch,
    .end_pass = gvkEndPass,
    .commit = gvkCommit,
    .query_pixelformat = gvkQueryPixelformat,
};

const gfx_backend_api* gfx_backend_vulkan(void) { return &vulkanApi; }

#endif /* SC_GPU_VULKAN */
