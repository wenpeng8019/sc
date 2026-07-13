/* ============================================================
 * vulkan_spc.c —— spc kernel 面：Vulkan compute（linux / android / win）
 * ============================================================
 * · 设备借自 gpu env（sc_gpu_device() → sc_gpu_vk_device* 聚合体），
 *   命令池 / 描述符池独立于 gfx
 * · 内核 = scc .ss comp 产物（tar vulkan@450 的 .spv 二进制）
 *   → VkShaderModule + VkComputePipeline
 * · 绑定对位：SPIR-V 天然保留 set/binding——反射清单的 binding 直接
 *   即 descriptor binding（无 MSL 那样的槽位重排问题）；一期全部资源
 *   限 set 0（scc 的 comp 产物惯例），set≥1 报不支持
 * · 缓冲：单块 host-visible|coherent 内存（读回免 flush），用途
 *   storage|transfer；uniform 小参数每次 dispatch 内联进独立
 *   uniform buffer（简化：按需临时创建，dispatch 后随 finish 释放）
 * · dispatch：gx/gy/gz = 全局线程数 → 组数 = ceil(g/local)（SPIR-V
 *   ExecutionMode LocalSize 已编入内核；反射 local 只用于换算组数）
 * · 特化常量：VkSpecializationInfo（constantID = 反射 spec_constants[].id，
 *   每项 4 字节；类型无关，按位传入）
 * · 同步模型（与 Metal 后端同构的 lastCmd 语义）：每次 dispatch 一个
 *   一次性命令缓冲 + fence；finish 等待全部在飞 fence
 *
 * 【板上调试指引】见 builtins/spc/PORTING.md（换设备必读）
 * ============================================================ */

#include "../../platform.h"   /* 平台判定宏（尊重交叉目标 SC_TARGET_*）；须先于守卫 */
#if P_LINUX || P_WIN

#include "internal.h"
#include "../../gpu/gpu_vk.h"   /* sc_gpu_vk_device + vk_loader（VK_NO_PROTOTYPES 动态加载） */
#include <stdlib.h>
#include <string.h>

extern void* sc_gpu_device(void);
extern int   sc_gpu_query_backend(void);

/* ---- 后端私有体 -------------------------------------------- */

typedef struct VkSpcBuffer {
    VkBuffer       buf;
    VkDeviceMemory mem;
    void*          mapped;      /* host-visible 常驻映射 */
    uint64_t       size;
} VkSpcBuffer;

typedef struct VkSpcKernel {
    VkShaderModule        shader;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout      layout;
    VkPipeline            pipeline;
    /* 每 binding 的描述符类型（uniform / storage），来自反射 */
    VkDescriptorType      dtype[SC_SPC_MAX_BINDINGS];
    bool                  used[SC_SPC_MAX_BINDINGS];
} VkSpcKernel;

/* 在飞命令：dispatch 提交后挂账，finish 统一等待回收 */
enum { VKSPC_MAX_INFLIGHT = 16 };
typedef struct VkSpcInflight {
    VkCommandBuffer cmd;
    VkFence         fence;
    VkDescriptorPool dpool;       /* 本次 dispatch 的临时描述符池 */
    VkBuffer        ubuf[SC_SPC_MAX_BINDINGS];        /* 临时 uniform 缓冲 */
    VkDeviceMemory  umem[SC_SPC_MAX_BINDINGS];
    int             ubuf_count;
    bool            busy;
} VkSpcInflight;

static struct {
    const sc_gpu_vk_device* dev;   /* 借自 gpu env（不拥有） */
    VkCommandPool  pool;
    VkSpcInflight  fl[VKSPC_MAX_INFLIGHT];
} V;

/* ---- 内存工具 ---------------------------------------------- */

static int vkspcFindMemType(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(V.dev->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return (int)i;
    return -1;
}

/* host-visible|coherent 缓冲（usage 由调用方给；成功后常驻映射到 *mapped） */
static bool vkspcMakeBuffer(uint64_t size, VkBufferUsageFlags usage,
                            VkBuffer* outBuf, VkDeviceMemory* outMem, void** mapped) {
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(V.dev->device, &bi, NULL, outBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(V.dev->device, *outBuf, &mr);
    int mt = vkspcFindMemType(mr.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt < 0) {
        spc_log("vulkan: 无 host-visible|coherent 内存类型");
        vkDestroyBuffer(V.dev->device, *outBuf, NULL);
        *outBuf = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = (uint32_t)mt;
    if (vkAllocateMemory(V.dev->device, &ai, NULL, outMem) != VK_SUCCESS ||
        vkBindBufferMemory(V.dev->device, *outBuf, *outMem, 0) != VK_SUCCESS) {
        spc_log("vulkan: 缓冲内存分配/绑定失败（size=%llu）", (unsigned long long)size);
        if (*outMem) vkFreeMemory(V.dev->device, *outMem, NULL);
        vkDestroyBuffer(V.dev->device, *outBuf, NULL);
        *outBuf = VK_NULL_HANDLE; *outMem = VK_NULL_HANDLE;
        return false;
    }
    if (mapped) {
        if (vkMapMemory(V.dev->device, *outMem, 0, VK_WHOLE_SIZE, 0, mapped) != VK_SUCCESS) {
            vkFreeMemory(V.dev->device, *outMem, NULL);
            vkDestroyBuffer(V.dev->device, *outBuf, NULL);
            *outBuf = VK_NULL_HANDLE; *outMem = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

/* 回收一个已完成的在飞槽（等待其 fence） */
static void vkspcReclaim(VkSpcInflight* f) {
    if (!f->busy) return;
    vkWaitForFences(V.dev->device, 1, &f->fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(V.dev->device, f->fence, NULL);
    vkFreeCommandBuffers(V.dev->device, V.pool, 1, &f->cmd);
    if (f->dpool) vkDestroyDescriptorPool(V.dev->device, f->dpool, NULL);
    for (int i = 0; i < f->ubuf_count; i++) {
        vkDestroyBuffer(V.dev->device, f->ubuf[i], NULL);
        vkFreeMemory(V.dev->device, f->umem[i], NULL);
    }
    memset(f, 0, sizeof(*f));
}

/* ---- 生命周期 ---------------------------------------------- */

static bool spc_vk_init(void) {
    memset(&V, 0, sizeof(V));
    V.dev = (const sc_gpu_vk_device*)sc_gpu_device();
    if (!V.dev || !V.dev->device) {
        spc_log("vulkan: gpu env 未交付设备聚合体（先 sc_gpu_init，backend=VULKAN）");
        return false;
    }
    VkCommandPoolCreateInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = V.dev->queue_family;
    if (vkCreateCommandPool(V.dev->device, &pi, NULL, &V.pool) != VK_SUCCESS) {
        spc_log("vulkan: 命令池创建失败");
        return false;
    }
    return true;
}

static void spc_vk_shutdown(void) {
    if (!V.dev) return;
    for (int i = 0; i < VKSPC_MAX_INFLIGHT; i++) vkspcReclaim(&V.fl[i]);
    if (V.pool) vkDestroyCommandPool(V.dev->device, V.pool, NULL);
    memset(&V, 0, sizeof(V));
}

static void spc_vk_finish(void) {
    for (int i = 0; i < VKSPC_MAX_INFLIGHT; i++) vkspcReclaim(&V.fl[i]);
}

/* ---- buffer ------------------------------------------------ */

static bool spc_vk_buffer_create(spc_buffer_t* b, const void* data, uint64_t size) {
    VkSpcBuffer* m = (VkSpcBuffer*)calloc(1, sizeof(VkSpcBuffer));
    if (!m) return false;
    if (!vkspcMakeBuffer(size,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &m->buf, &m->mem, &m->mapped)) {
        free(m);
        return false;
    }
    m->size = size;
    if (data) memcpy(m->mapped, data, size);
    b->backend = m;
    return true;
}

static void spc_vk_buffer_destroy(spc_buffer_t* b) {
    VkSpcBuffer* m = (VkSpcBuffer*)b->backend;
    if (!m) return;
    spc_vk_finish();   /* 防销毁在飞资源 */
    vkUnmapMemory(V.dev->device, m->mem);
    vkDestroyBuffer(V.dev->device, m->buf, NULL);
    vkFreeMemory(V.dev->device, m->mem, NULL);
    free(m);
    b->backend = NULL;
}

static bool spc_vk_buffer_read(spc_buffer_t* b, void* dst, uint64_t size, uint64_t off) {
    VkSpcBuffer* m = (VkSpcBuffer*)b->backend;
    if (!m) return false;
    spc_vk_finish();   /* 读回前确保 GPU 写入完成（coherent 内存免 invalidate） */
    memcpy(dst, (const uint8_t*)m->mapped + off, size);
    return true;
}

static bool spc_vk_buffer_write(spc_buffer_t* b, const void* src, uint64_t size, uint64_t off) {
    VkSpcBuffer* m = (VkSpcBuffer*)b->backend;
    if (!m) return false;
    memcpy((uint8_t*)m->mapped + off, src, size);
    return true;
}

/* ---- kernel ------------------------------------------------ */

static bool spc_vk_kernel_create(spc_kernel_t* k, const sc_spc_kernel_desc* desc) {
    if (desc->code.size < 4 || ((const uint8_t*)desc->code.ptr)[0] != 0x03) {
        /* SPIR-V magic 0x07230203 小端首字节 0x03——喂成 MSL/GLSL 文本时快速失败 */
        spc_log("vulkan: 内核 code 不是 SPIR-V（须用 tar vulkan@450 产物条目）");
        return false;
    }
    VkSpcKernel* m = (VkSpcKernel*)calloc(1, sizeof(VkSpcKernel));
    if (!m) return false;

    VkShaderModuleCreateInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    si.codeSize = (size_t)desc->code.size;
    si.pCode = (const uint32_t*)desc->code.ptr;
    if (vkCreateShaderModule(V.dev->device, &si, NULL, &m->shader) != VK_SUCCESS) {
        spc_log("vulkan: VkShaderModule 创建失败（.spv 损坏？）");
        free(m);
        return false;
    }

    /* 描述符布局：反射清单 binding → uniform/storage（全 set 0） */
    VkDescriptorSetLayoutBinding binds[SC_SPC_MAX_BINDINGS];
    memset(binds, 0, sizeof(binds));
    uint32_t nb = 0;
    for (int i = 0; i < k->res_count; i++) {
        const spc_kernel_res* r = &k->res[i];
        m->dtype[r->binding] = r->storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                          : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        m->used[r->binding] = true;
        binds[nb].binding = (uint32_t)r->binding;
        binds[nb].descriptorType = m->dtype[r->binding];
        binds[nb].descriptorCount = 1;
        binds[nb].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        nb++;
    }
    VkDescriptorSetLayoutCreateInfo di;
    memset(&di, 0, sizeof(di));
    di.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    di.bindingCount = nb;
    di.pBindings = binds;
    if (vkCreateDescriptorSetLayout(V.dev->device, &di, NULL, &m->dsl) != VK_SUCCESS)
        goto fail;

    {
        VkPipelineLayoutCreateInfo li;
        memset(&li, 0, sizeof(li));
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1;
        li.pSetLayouts = &m->dsl;
        if (vkCreatePipelineLayout(V.dev->device, &li, NULL, &m->layout) != VK_SUCCESS)
            goto fail;
    }

    {
        /* 特化常量：VkSpecializationInfo（每项 4 字节，按位传值，类型无关） */
        VkSpecializationMapEntry entries[SC_SPC_MAX_BINDINGS];
        uint32_t datas[SC_SPC_MAX_BINDINGS];
        VkSpecializationInfo spec;
        memset(&spec, 0, sizeof(spec));
        VkComputePipelineCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = m->shader;
        ci.stage.pName = "main";        /* scc SPIR-V 入口恒 main（entry 名只用于产物文件名） */
        if (desc->spec_count > 0 && desc->spec_values) {
            int n = desc->spec_count;
            if (n > SC_SPC_MAX_BINDINGS) n = SC_SPC_MAX_BINDINGS;
            for (int i = 0; i < n; i++) {
                entries[i].constantID = (uint32_t)desc->spec_values[i].id;
                entries[i].offset = (uint32_t)(i * 4);
                entries[i].size = 4;
                datas[i] = desc->spec_values[i].value;
            }
            spec.mapEntryCount = (uint32_t)n;
            spec.pMapEntries = entries;
            spec.dataSize = (size_t)(n * 4);
            spec.pData = datas;
            ci.stage.pSpecializationInfo = &spec;
        }
        if (vkCreateComputePipelines(V.dev->device, VK_NULL_HANDLE, 1, &ci, NULL,
                                     &m->pipeline) != VK_SUCCESS) {
            spc_log("vulkan: 计算管线创建失败");
            goto fail;
        }
    }
    k->backend = m;
    return true;

fail:
    if (m->layout) vkDestroyPipelineLayout(V.dev->device, m->layout, NULL);
    if (m->dsl) vkDestroyDescriptorSetLayout(V.dev->device, m->dsl, NULL);
    if (m->shader) vkDestroyShaderModule(V.dev->device, m->shader, NULL);
    free(m);
    return false;
}

static void spc_vk_kernel_destroy(spc_kernel_t* k) {
    VkSpcKernel* m = (VkSpcKernel*)k->backend;
    if (!m) return;
    spc_vk_finish();
    vkDestroyPipeline(V.dev->device, m->pipeline, NULL);
    vkDestroyPipelineLayout(V.dev->device, m->layout, NULL);
    vkDestroyDescriptorSetLayout(V.dev->device, m->dsl, NULL);
    vkDestroyShaderModule(V.dev->device, m->shader, NULL);
    free(m);
    k->backend = NULL;
}

static bool spc_vk_dispatch(spc_kernel_t* k, int gx, int gy, int gz,
                            const sc_spc_bindings* bnd,
                            spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]) {
    VkSpcKernel* m = (VkSpcKernel*)k->backend;
    if (!m) return false;

    /* 找空闲在飞槽（满则回收最老的一个） */
    VkSpcInflight* f = NULL;
    for (int i = 0; i < VKSPC_MAX_INFLIGHT; i++)
        if (!V.fl[i].busy) { f = &V.fl[i]; break; }
    if (!f) { vkspcReclaim(&V.fl[0]); f = &V.fl[0]; }
    memset(f, 0, sizeof(*f));

    /* 临时描述符池 + set */
    VkDescriptorPoolSize sizes[2];
    memset(sizes, 0, sizeof(sizes));
    sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[0].descriptorCount = SC_SPC_MAX_BINDINGS;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[1].descriptorCount = SC_SPC_MAX_BINDINGS;
    VkDescriptorPoolCreateInfo dpi;
    memset(&dpi, 0, sizeof(dpi));
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(V.dev->device, &dpi, NULL, &f->dpool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dai;
    memset(&dai, 0, sizeof(dai));
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = f->dpool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &m->dsl;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(V.dev->device, &dai, &dset) != VK_SUCCESS)
        goto fail;

    /* 写描述符：storage 直连缓冲；uniform 内联字节 → 临时 uniform buffer */
    {
        VkWriteDescriptorSet writes[SC_SPC_MAX_BINDINGS];
        VkDescriptorBufferInfo infos[SC_SPC_MAX_BINDINGS];
        memset(writes, 0, sizeof(writes));
        memset(infos, 0, sizeof(infos));
        uint32_t nw = 0;
        for (int i = 0; i < k->res_count; i++) {
            const spc_kernel_res* r = &k->res[i];
            if (r->storage) {
                VkSpcBuffer* sb = (VkSpcBuffer*)bufs[r->binding]->backend;
                infos[nw].buffer = sb->buf;
                infos[nw].range = VK_WHOLE_SIZE;
            } else {
                const sc_spc_range* u = &bnd->uniforms[r->binding];
                VkBuffer ub = VK_NULL_HANDLE;
                VkDeviceMemory um = VK_NULL_HANDLE;
                void* map = NULL;
                if (!vkspcMakeBuffer(u->size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     &ub, &um, &map))
                    goto fail;
                memcpy(map, u->ptr, u->size);
                vkUnmapMemory(V.dev->device, um);
                f->ubuf[f->ubuf_count] = ub;
                f->umem[f->ubuf_count] = um;
                f->ubuf_count++;
                infos[nw].buffer = ub;
                infos[nw].range = VK_WHOLE_SIZE;
            }
            writes[nw].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[nw].dstSet = dset;
            writes[nw].dstBinding = (uint32_t)r->binding;
            writes[nw].descriptorCount = 1;
            writes[nw].descriptorType = m->dtype[r->binding];
            writes[nw].pBufferInfo = &infos[nw];
            nw++;
        }
        vkUpdateDescriptorSets(V.dev->device, nw, writes, 0, NULL);
    }

    /* 命令缓冲：bind + dispatch + host 读回屏障 */
    {
        VkCommandBufferAllocateInfo cai;
        memset(&cai, 0, sizeof(cai));
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = V.pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(V.dev->device, &cai, &f->cmd) != VK_SUCCESS)
            goto fail;
        VkCommandBufferBeginInfo bi2;
        memset(&bi2, 0, sizeof(bi2));
        bi2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi2.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(f->cmd, &bi2);
        vkCmdBindPipeline(f->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m->pipeline);
        vkCmdBindDescriptorSets(f->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m->layout,
                                0, 1, &dset, 0, NULL);
        uint32_t nx = (uint32_t)((gx + k->local[0] - 1) / k->local[0]);
        uint32_t ny = (uint32_t)((gy + k->local[1] - 1) / k->local[1]);
        uint32_t nz = (uint32_t)((gz + k->local[2] - 1) / k->local[2]);
        vkCmdDispatch(f->cmd, nx ? nx : 1, ny ? ny : 1, nz ? nz : 1);
        /* compute 写 → host 读 屏障（coherent 内存 + fence 等待后 CPU 可见） */
        VkMemoryBarrier mb;
        memset(&mb, 0, sizeof(mb));
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(f->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        vkEndCommandBuffer(f->cmd);
    }

    {
        VkFenceCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(V.dev->device, &fi, NULL, &f->fence) != VK_SUCCESS)
            goto fail;
        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f->cmd;
        if (vkQueueSubmit(V.dev->queue, 1, &si, f->fence) != VK_SUCCESS) {
            spc_log("vulkan: 队列提交失败");
            goto fail;
        }
    }
    f->busy = true;
    return true;

fail:
    /* 未提交即失败：手动回收本槽已建资源 */
    if (f->fence) vkDestroyFence(V.dev->device, f->fence, NULL);
    if (f->cmd) vkFreeCommandBuffers(V.dev->device, V.pool, 1, &f->cmd);
    if (f->dpool) vkDestroyDescriptorPool(V.dev->device, f->dpool, NULL);
    for (int i = 0; i < f->ubuf_count; i++) {
        vkDestroyBuffer(V.dev->device, f->ubuf[i], NULL);
        vkFreeMemory(V.dev->device, f->umem[i], NULL);
    }
    memset(f, 0, sizeof(*f));
    return false;
}

/* ---- vtable ------------------------------------------------ */

const spc_kernel_api* spc_vk_api(void) {
    static const spc_kernel_api api = {
        "vulkan",
        spc_vk_init, spc_vk_shutdown, spc_vk_finish,
        spc_vk_buffer_create, spc_vk_buffer_destroy,
        spc_vk_buffer_read, spc_vk_buffer_write,
        spc_vk_kernel_create, spc_vk_kernel_destroy,
        spc_vk_dispatch,
    };
    return &api;
}

#endif /* P_LINUX || P_WIN */
