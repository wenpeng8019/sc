// ============================================================
// syntax-g 运行时 demo：GLSL → SPIR-V → MoltenVK 三角形
// ------------------------------------------------------------
// 管线：scc tri.sg → *.vert/*.frag (Vulkan-GLSL)
//        → glslangValidator → *.spv
//        → 本 host 用 Vulkan(经 MoltenVK) + GLFW 加载 .spv 并绘制三角形。
// host 不含任何着色器逻辑：顶点由着色器内 vertex_id 索引常量数组生成，
// 因此只需一个空的 draw(3)。参见 build.sh 与 README.md。
// ============================================================
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WIDTH  800
#define HEIGHT 600
#define MAX_FRAMES 2

static void die(const char* msg) { fprintf(stderr, "错误: %s\n", msg); exit(1); }
#define VKCHECK(x) do { if ((x) != VK_SUCCESS) die(#x); } while (0)

// 读取整个文件到内存（SPIR-V 二进制）。
static uint32_t* read_file(const char* path, size_t* size_out) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "无法打开 %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t* buf = malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) die("读取着色器失败");
    fclose(f);
    *size_out = (size_t)sz;
    return buf;
}

static VkShaderModule load_shader(VkDevice dev, const char* path) {
    size_t sz;
    uint32_t* code = read_file(path, &sz);
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = sz;
    ci.pCode = code;
    VkShaderModule m;
    VKCHECK(vkCreateShaderModule(dev, &ci, NULL, &m));
    free(code);
    return m;
}

int main(int argc, char** argv) {
    const char* vs_path = argc > 1 ? argv[1] : "build/vs_main.spv";
    const char* fs_path = argc > 2 ? argv[2] : "build/fs_main.spv";

    if (!glfwInit()) die("glfwInit");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(WIDTH, HEIGHT, "sc syntax-g · MoltenVK triangle", NULL, NULL);
    if (!win) die("glfwCreateWindow");

    // ---- 实例（MoltenVK 需 portability enumeration）----
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    const char* exts[16];
    uint32_t extCount = 0;
    for (uint32_t i = 0; i < glfwExtCount; i++) exts[extCount++] = glfwExts[i];
    exts[extCount++] = "VK_KHR_portability_enumeration";
    exts[extCount++] = "VK_KHR_get_physical_device_properties2";

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "sc-syntax-g-tri";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.flags = 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;
    VkInstance inst;
    VKCHECK(vkCreateInstance(&ici, NULL, &inst));

    VkSurfaceKHR surface;
    VKCHECK(glfwCreateWindowSurface(inst, win, NULL, &surface));

    // ---- 物理设备 + 队列族 ----
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(inst, &gpuCount, NULL);
    if (!gpuCount) die("未找到 Vulkan 设备");
    VkPhysicalDevice gpus[8];
    if (gpuCount > 8) gpuCount = 8;
    vkEnumeratePhysicalDevices(inst, &gpuCount, gpus);
    VkPhysicalDevice gpu = gpus[0];

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qCount, NULL);
    VkQueueFamilyProperties qprops[16];
    if (qCount > 16) qCount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qCount, qprops);
    uint32_t qfam = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { qfam = i; break; }
    }
    if (qfam == UINT32_MAX) die("无合适队列族");

    // ---- 逻辑设备 ----
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    // 仅在设备支持时才启用 VK_KHR_portability_subset（MoltenVK 需要，软件驱动无）。
    const char* devExts[4];
    uint32_t devExtCount = 0;
    devExts[devExtCount++] = "VK_KHR_swapchain";
    {
        uint32_t availCount = 0;
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &availCount, NULL);
        VkExtensionProperties* avail = malloc(availCount * sizeof(*avail));
        vkEnumerateDeviceExtensionProperties(gpu, NULL, &availCount, avail);
        for (uint32_t i = 0; i < availCount; i++)
            if (strcmp(avail[i].extensionName, "VK_KHR_portability_subset") == 0) {
                devExts[devExtCount++] = "VK_KHR_portability_subset";
                break;
            }
        free(avail);
    }
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = devExtCount;
    dci.ppEnabledExtensionNames = devExts;
    VkDevice dev;
    VKCHECK(vkCreateDevice(gpu, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    // ---- 交换链 ----
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    VkSurfaceFormatKHR fmts[32];
    uint32_t fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmtCount, fmts);
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < fmtCount; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { fmt = fmts[i]; break; }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { extent.width = WIDTH; extent.height = HEIGHT; }
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sci.surface = surface;
    sci.minImageCount = imgCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VkSwapchainKHR swapchain;
    VKCHECK(vkCreateSwapchainKHR(dev, &sci, NULL, &swapchain));

    VkImage images[8];
    vkGetSwapchainImagesKHR(dev, swapchain, &imgCount, NULL);
    if (imgCount > 8) imgCount = 8;
    vkGetSwapchainImagesKHR(dev, swapchain, &imgCount, images);

    VkImageView views[8];
    for (uint32_t i = 0; i < imgCount; i++) {
        VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt.format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        VKCHECK(vkCreateImageView(dev, &vci, NULL, &views[i]));
    }

    // ---- 渲染通道 ----
    VkAttachmentDescription color = {0};
    color.format = fmt.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    VkRenderPass renderPass;
    VKCHECK(vkCreateRenderPass(dev, &rpci, NULL, &renderPass));

    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < imgCount; i++) {
        VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &views[i];
        fci.width = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev, &fci, NULL, &framebuffers[i]));
    }

    // ---- 图形管线（无顶点输入）----
    VkShaderModule vs = load_shader(dev, vs_path);
    VkShaderModule fs = load_shader(dev, fs_path);
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
          VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL },
    };
    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport = { 0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, extent };
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.pViewports = &viewport;
    vp.scissorCount = 1; vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pipelineLayout;
    VKCHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pipelineLayout));
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.layout = pipelineLayout;
    gpci.renderPass = renderPass;
    VkPipeline pipeline;
    VKCHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));

    // ---- 命令池 + 同步 ----
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VkCommandPool cmdPool;
    VKCHECK(vkCreateCommandPool(dev, &cpci, NULL, &cmdPool));
    VkCommandBuffer cmds[MAX_FRAMES];
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = MAX_FRAMES;
    VKCHECK(vkAllocateCommandBuffers(dev, &cbai, cmds));

    VkSemaphore imgAvail[MAX_FRAMES], renderDone[MAX_FRAMES];
    VkFence inFlight[MAX_FRAMES];
    for (int i = 0; i < MAX_FRAMES; i++) {
        VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fnci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fnci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKCHECK(vkCreateSemaphore(dev, &semci, NULL, &imgAvail[i]));
        VKCHECK(vkCreateSemaphore(dev, &semci, NULL, &renderDone[i]));
        VKCHECK(vkCreateFence(dev, &fnci, NULL, &inFlight[i]));
    }

    // ---- 渲染循环 ----
    uint32_t frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        vkWaitForFences(dev, 1, &inFlight[frame], VK_TRUE, UINT64_MAX);
        vkResetFences(dev, 1, &inFlight[frame]);

        uint32_t idx;
        vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, imgAvail[frame], VK_NULL_HANDLE, &idx);

        VkCommandBuffer cmd = cmds[frame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);
        VkClearValue clear = { .color = { { 0.02f, 0.02f, 0.05f, 1.0f } } };
        VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpbi.renderPass = renderPass;
        rpbi.framebuffer = framebuffers[idx];
        rpbi.renderArea.extent = extent;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imgAvail[frame];
        submit.pWaitDstStageMask = &wait;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderDone[frame];
        VKCHECK(vkQueueSubmit(queue, 1, &submit, inFlight[frame]));

        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderDone[frame];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &idx;
        vkQueuePresentKHR(queue, &present);

        frame = (frame + 1) % MAX_FRAMES;
    }
    vkDeviceWaitIdle(dev);

    // ---- 清理 ----
    for (int i = 0; i < MAX_FRAMES; i++) {
        vkDestroySemaphore(dev, imgAvail[i], NULL);
        vkDestroySemaphore(dev, renderDone[i], NULL);
        vkDestroyFence(dev, inFlight[i], NULL);
    }
    vkDestroyCommandPool(dev, cmdPool, NULL);
    vkDestroyPipeline(dev, pipeline, NULL);
    vkDestroyPipelineLayout(dev, pipelineLayout, NULL);
    vkDestroyShaderModule(dev, vs, NULL);
    vkDestroyShaderModule(dev, fs, NULL);
    for (uint32_t i = 0; i < imgCount; i++) {
        vkDestroyFramebuffer(dev, framebuffers[i], NULL);
        vkDestroyImageView(dev, views[i], NULL);
    }
    vkDestroyRenderPass(dev, renderPass, NULL);
    vkDestroySwapchainKHR(dev, swapchain, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroySurfaceKHR(inst, surface, NULL);
    vkDestroyInstance(inst, NULL);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
