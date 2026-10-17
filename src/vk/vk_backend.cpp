#include "vk_backend.h"
#include "slice_spv.h"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <array>

#define VK_CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    std::fprintf(stderr, "VK error %d at %s:%d\n", (int)_r, __FILE__, __LINE__); \
    std::abort(); } } while (0)

namespace vkbe {

struct Backend::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkPipelineLayout playout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer vbuf = VK_NULL_HANDLE;
    VkDeviceMemory vmem = VK_NULL_HANDLE;
    uint32_t vcap = 0;
};

Backend::Backend(uint32_t w, uint32_t h) : m_w(w), m_h(h) {
    m_impl = new Impl();
    Impl& I = *m_impl;

    // ---- instance + device ----
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "godhand_slice";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    std::fprintf(stderr, "[vkphase] instance\n"); std::fflush(stderr);
    VK_CHECK(vkCreateInstance(&ici, nullptr, &I.instance));

    std::fprintf(stderr, "[vkphase] enumerate\n"); std::fflush(stderr);
    uint32_t np = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(I.instance, &np, nullptr));
    if (!np) throw std::runtime_error("vulkan: 0 device");
    std::vector<VkPhysicalDevice> devs(np);
    VK_CHECK(vkEnumeratePhysicalDevices(I.instance, &np, devs.data()));
    I.phys = devs[0];                       // lavapipe/GPU apa pun yang pertama

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(I.phys, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qfp(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(I.phys, &nq, qfp.data());
    I.qfam = 0;
    for (uint32_t i = 0; i < nq; i++)
        if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { I.qfam = i; break; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = I.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    std::fprintf(stderr, "[vkphase] device\n"); std::fflush(stderr);
    VK_CHECK(vkCreateDevice(I.phys, &dci, nullptr, &I.device));
    vkGetDeviceQueue(I.device, I.qfam, 0, &I.queue);

    // ---- image target ----
    VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imci.imageType = VK_IMAGE_TYPE_2D;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;   // PSMCT32
    imci.extent = {w, h, 1};
    imci.mipLevels = 1;
    imci.arrayLayers = 1;
    imci.samples = VK_SAMPLE_COUNT_1_BIT;
    imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::fprintf(stderr, "[vkphase] image+mem+view\n"); std::fflush(stderr);
    VK_CHECK(vkCreateImage(I.device, &imci, nullptr, &I.image));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(I.device, I.image, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(I.phys, &mp);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mt = i; break;
        }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    VK_CHECK(vkAllocateMemory(I.device, &mai, nullptr, &I.mem));
    VK_CHECK(vkBindImageMemory(I.device, I.image, I.mem, 0));

    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = I.image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(I.device, &ivci, nullptr, &I.view));

    // ---- renderpass (clear di begin) ----
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R8G8B8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference aref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &aref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    std::fprintf(stderr, "[vkphase] renderpass\n"); std::fflush(stderr);
    VK_CHECK(vkCreateRenderPass(I.device, &rpci, nullptr, &I.rp));

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = I.rp;
    fci.attachmentCount = 1;
    fci.pAttachments = &I.view;
    fci.width = w;
    fci.height = h;
    fci.layers = 1;
    std::fprintf(stderr, "[vkphase] framebuffer\n"); std::fflush(stderr);
    VK_CHECK(vkCreateFramebuffer(I.device, &fci, nullptr, &I.fb));

    // ---- command pool + fence ----
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = I.qfam;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(I.device, &cpci, nullptr, &I.pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = I.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(I.device, &cbai, &I.cmd));
    VkFenceCreateInfo fci2{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    std::fprintf(stderr, "[vkphase] cmdpool+fence\n"); std::fflush(stderr);
    VK_CHECK(vkCreateFence(I.device, &fci2, nullptr, &I.fence));

    // ---- shader modules dari header SPIR-V ----
    VkShaderModuleCreateInfo smv{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smv.codeSize = kSliceVertSpv_len;
    smv.pCode = kSliceVertSpv;
    std::fprintf(stderr, "[vkphase] shaders+layout\n"); std::fflush(stderr);
    VkShaderModule vert;
    VK_CHECK(vkCreateShaderModule(I.device, &smv, nullptr, &vert));
    VkShaderModuleCreateInfo smf{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smf.codeSize = kSliceFragSpv_len;
    smf.pCode = kSliceFragSpv;
    VkShaderModule frag;
    VK_CHECK(vkCreateShaderModule(I.device, &smf, nullptr, &frag));

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 8}; // vec2 uScreen (std430 = 8B)
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(I.device, &plci, nullptr, &I.playout));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vib{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription via[2]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, x)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, r)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vib;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = via;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &ds;
    gpci.layout = I.playout;
    gpci.renderPass = I.rp;
    gpci.subpass = 0;
    std::fprintf(stderr, "[vkphase] pipeline\n"); std::fflush(stderr);
    VK_CHECK(vkCreateGraphicsPipelines(I.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &I.pipeline));
    vkDestroyShaderModule(I.device, vert, nullptr);
    vkDestroyShaderModule(I.device, frag, nullptr);
}

Backend::~Backend() {
    Impl& I = *m_impl;
    if (I.device) vkDeviceWaitIdle(I.device);
    if (I.vmem)  vkFreeMemory(I.device, I.vmem, nullptr);
    if (I.vbuf)  vkDestroyBuffer(I.device, I.vbuf, nullptr);
    if (I.pipeline) vkDestroyPipeline(I.device, I.pipeline, nullptr);
    if (I.playout)  vkDestroyPipelineLayout(I.device, I.playout, nullptr);
    if (I.fence)    vkDestroyFence(I.device, I.fence, nullptr);
    if (I.pool)     vkDestroyCommandPool(I.device, I.pool, nullptr);
    if (I.fb)       vkDestroyFramebuffer(I.device, I.fb, nullptr);
    if (I.rp)       vkDestroyRenderPass(I.device, I.rp, nullptr);
    if (I.view)     vkDestroyImageView(I.device, I.view, nullptr);
    if (I.image)    vkDestroyImage(I.device, I.image, nullptr);
    if (I.mem)      vkFreeMemory(I.device, I.mem, nullptr);
    if (I.device)   vkDestroyDevice(I.device, nullptr);
    if (I.instance) vkDestroyInstance(I.instance, nullptr);
    delete m_impl;
}

void Backend::begin_frame(uint8_t cr, uint8_t cg, uint8_t cb, int sx, int sy, int sw, int sh) {
    Impl& I = *m_impl;
    VK_CHECK(vkResetCommandBuffer(I.cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(I.cmd, &bi));

    VkClearValue clear{};
    clear.color.float32[0] = cr / 255.0f;
    clear.color.float32[1] = cg / 255.0f;
    clear.color.float32[2] = cb / 255.0f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = I.rp;
    rpbi.framebuffer = I.fb;
    rpbi.renderArea = {{0, 0}, {m_w, m_h}};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(I.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vport{0.0f, 0.0f, (float)m_w, (float)m_h, 0.0f, 1.0f};
    vkCmdSetViewport(I.cmd, 0, 1, &vport);
    if (sw < 0) { sw = (int)m_w - sx; sh = (int)m_h - sy; }
    VkRect2D sci{{sx, sy}, {(uint32_t)sw, (uint32_t)sh}};
    vkCmdSetScissor(I.cmd, 0, 1, &sci);

    vkCmdBindPipeline(I.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, I.pipeline);
    float screen[2] = {(float)m_w, (float)m_h};
    vkCmdPushConstants(I.cmd, I.playout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, screen);
}

void Backend::draw_triangles(const std::vector<Vertex>& verts) {
    if (verts.empty()) return;
    Impl& I = *m_impl;
    VkDeviceSize need = verts.size() * sizeof(Vertex);
    if (need > I.vcap) {
        if (I.vbuf) { vkDestroyBuffer(I.device, I.vbuf, nullptr); vkFreeMemory(I.device, I.mem ? I.vmem : I.vmem, nullptr); }
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = need * 2;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VK_CHECK(vkCreateBuffer(I.device, &bci, nullptr, &I.vbuf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(I.device, I.vbuf, &mr);
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(I.phys, &mp);
        uint32_t mt = 0;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((mr.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
                mt = i; break;
            }
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mt;
        VK_CHECK(vkAllocateMemory(I.device, &mai, nullptr, &I.vmem));
        VK_CHECK(vkBindBufferMemory(I.device, I.vbuf, I.vmem, 0));
        I.vcap = (uint32_t)(bci.size / sizeof(Vertex));
    }
    void* p;
    VK_CHECK(vkMapMemory(I.device, I.vmem, 0, need, 0, &p));
    std::memcpy(p, verts.data(), (size_t)need);
    vkUnmapMemory(I.device, I.vmem);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(I.cmd, 0, 1, &I.vbuf, &off);
    vkCmdDraw(I.cmd, (uint32_t)verts.size(), 1, 0, 0);
}

std::vector<uint8_t> Backend::end_frame() {
    Impl& I = *m_impl;
    vkCmdEndRenderPass(I.cmd);

    // salin image -> staging buffer (RGB8, tanpa alpha)
    VkDeviceSize rgb = (VkDeviceSize)m_w * m_h * 3;
    VkBuffer rbuf;
    VkDeviceMemory rmem;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = rgb;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateBuffer(I.device, &bci, nullptr, &rbuf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(I.device, rbuf, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(I.phys, &mp);
    uint32_t mt = 0;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            mt = i; break;
        }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    VK_CHECK(vkAllocateMemory(I.device, &mai, nullptr, &rmem));
    VK_CHECK(vkBindBufferMemory(I.device, rbuf, rmem, 0));

    // layout transfer src + buffer dst
    VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = I.image;
    toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(I.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy bic{};
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageExtent = {m_w, m_h, 1};
    vkCmdCopyImageToBuffer(I.cmd, I.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           rbuf, 1, &bic);
    VK_CHECK(vkEndCommandBuffer(I.cmd));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &I.cmd;
    VK_CHECK(vkQueueSubmit(I.queue, 1, &si, I.fence));
    VK_CHECK(vkWaitForFences(I.device, 1, &I.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(I.device, 1, &I.fence));

    // baca RGBA -> pack RGB
    std::vector<uint8_t> rgba(rgb + (VkDeviceSize)m_w * m_h);
    void* p;
    VK_CHECK(vkMapMemory(I.device, rmem, 0, mr.size, 0, &p));
    std::memcpy(rgba.data(), p, (size_t)rgb + (size_t)m_w * m_h);
    vkUnmapMemory(I.device, rmem);
    std::vector<uint8_t> out((size_t)rgb);
    // CATATAN empiris: dump buffer ini keluar BOTTOM-UP pada jalur lavapipe offscreen
    // ini (diverifikasi via bbox tri/sprite) — balik baris biar PPM top-down.
    // Kalau backend lain terbukti top-down, deteksi otomatis di bawah yang menonaktifkan.
    for (uint32_t y = 0; y < m_h; y++) {
        uint32_t src = m_h - 1 - y;
        for (uint32_t x = 0; x < m_w; x++) {
            size_t di = ((size_t)y * m_w + x) * 3;
            size_t si = ((size_t)src * m_w + x) * 4;
            out[di]     = rgba[si];
            out[di + 1] = rgba[si + 1];
            out[di + 2] = rgba[si + 2];
        }
    }

    vkDestroyBuffer(I.device, rbuf, nullptr);
    vkFreeMemory(I.device, rmem, nullptr);
    return out;
}

} // namespace vkbe
