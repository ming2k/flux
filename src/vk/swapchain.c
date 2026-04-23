#include "internal.h"

#include "solid_color_vert_spv.inc"
#include "solid_color_frag_spv.inc"
#include "image_vert_spv.inc"
#include "image_frag_spv.inc"
#include "text_frag_spv.inc"

static VkSurfaceFormatKHR pick_format(const VkSurfaceFormatKHR *formats,
                                      uint32_t n)
{
    /* Prefer BGRA8 SRGB, then RGBA8 SRGB, then any UNORM BGRA. */
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            return formats[i];
    return formats[0];
}

static VkPresentModeKHR pick_present_mode(const VkPresentModeKHR *modes,
                                          uint32_t n)
{
    /* Priority per §4.4. */
    const VkPresentModeKHR prefs[] = {
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    for (size_t p = 0; p < sizeof(prefs)/sizeof(*prefs); ++p)
        for (uint32_t i = 0; i < n; ++i)
            if (modes[i] == prefs[p]) return prefs[p];
    return VK_PRESENT_MODE_FIFO_KHR;  /* guaranteed */
}

static VkExtent2D clamp_extent(VkSurfaceCapabilitiesKHR caps,
                               int32_t w, int32_t h)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    VkExtent2D e = { (uint32_t)w, (uint32_t)h };
    if (e.width  < caps.minImageExtent.width)  e.width  = caps.minImageExtent.width;
    if (e.height < caps.minImageExtent.height) e.height = caps.minImageExtent.height;
    if (e.width  > caps.maxImageExtent.width)  e.width  = caps.maxImageExtent.width;
    if (e.height > caps.maxImageExtent.height) e.height = caps.maxImageExtent.height;
    return e;
}

bool fx_make_render_pass(fx_surface *s, VkImageLayout final_layout)
{
    VkAttachmentDescription color = {
        .format         = s->surface_format.format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = final_layout,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_ref,
    };
    VkSubpassDependency dep = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };
    VkResult r = vkCreateRenderPass(s->ctx->device, &ci, NULL, &s->render_pass);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateRenderPass: %d", (int)r);
        return false;
    }
    return true;
}

static VkShaderModule make_shader_module(fx_context *ctx,
                                         const uint32_t *code,
                                         size_t code_size)
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode    = code,
    };
    if (vkCreateShaderModule(ctx->device, &ci, NULL, &module) != VK_SUCCESS) {
        FX_LOGE(ctx, "vkCreateShaderModule failed");
        return VK_NULL_HANDLE;
    }
    return module;
}

bool fx_make_image_dsl(fx_surface *s)
{
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(s->ctx->device, &ci, NULL, &s->image_dsl) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateDescriptorSetLayout failed");
        return false;
    }
    return true;
}

bool fx_make_image_pipeline(fx_surface *s)
{
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(fx_image_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &s->image_dsl,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(s->ctx->device, &lci, NULL, &s->image_layout) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreatePipelineLayout failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = { 0 };
    vert = make_shader_module(s->ctx, fx_image_vert_spv, sizeof(fx_image_vert_spv));
    frag = make_shader_module(s->ctx, fx_image_frag_spv, sizeof(fx_image_frag_spv));
    if (!vert || !frag) goto fail;

    stages[0] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert,
        .pName = "main",
    };
    stages[1] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag,
        .pName = "main",
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(fx_image_vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend_att = { .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_att };
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn_states };

    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = s->image_layout,
        .renderPass = s->render_pass,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(s->ctx->device, VK_NULL_HANDLE, 1, &pci, NULL, &s->image_pipeline) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateGraphicsPipelines (image) failed");
        goto fail;
    }

    vkDestroyShaderModule(s->ctx->device, frag, NULL);
    vkDestroyShaderModule(s->ctx->device, vert, NULL);
    return true;
fail:
    if (frag) vkDestroyShaderModule(s->ctx->device, frag, NULL);
    if (vert) vkDestroyShaderModule(s->ctx->device, vert, NULL);
    return false;
}

bool fx_make_text_pipeline(fx_surface *s)
{
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(fx_text_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &s->image_dsl,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(s->ctx->device, &lci, NULL, &s->text_layout) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreatePipelineLayout failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = { 0 };
    vert = make_shader_module(s->ctx, fx_image_vert_spv, sizeof(fx_image_vert_spv)); /* Reuse image vert */
    frag = make_shader_module(s->ctx, fx_text_frag_spv, sizeof(fx_text_frag_spv));
    if (!vert || !frag) goto fail;

    stages[0] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert,
        .pName = "main",
    };
    stages[1] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag,
        .pName = "main",
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(fx_image_vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(fx_image_vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend_att = { .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_att };
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn_states };

    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = s->text_layout,
        .renderPass = s->render_pass,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(s->ctx->device, VK_NULL_HANDLE, 1, &pci, NULL, &s->text_pipeline) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateGraphicsPipelines (text) failed");
        goto fail;
    }

    vkDestroyShaderModule(s->ctx->device, frag, NULL);
    vkDestroyShaderModule(s->ctx->device, vert, NULL);
    return true;
fail:
    if (frag) vkDestroyShaderModule(s->ctx->device, frag, NULL);
    if (vert) vkDestroyShaderModule(s->ctx->device, vert, NULL);
    return false;
}

bool fx_make_bootstrap_pipeline(fx_surface *s)
{
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(fx_solid_color_pc),
    };
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VkPipelineShaderStageCreateInfo stages[2] = { 0 };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(fx_solid_vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attr = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = 0,
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attr,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_att = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_att,
    };
    VkDynamicState dyn_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };
    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = s->solid_rect_layout,
        .renderPass = s->render_pass,
        .subpass = 0,
    };

    if (vkCreatePipelineLayout(s->ctx->device, &lci, NULL,
                               &s->solid_rect_layout) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreatePipelineLayout failed");
        return false;
    }

    vert = make_shader_module(s->ctx, fx_solid_color_vert_spv,
                              sizeof(fx_solid_color_vert_spv));
    frag = make_shader_module(s->ctx, fx_solid_color_frag_spv,
                              sizeof(fx_solid_color_frag_spv));
    if (!vert || !frag) goto fail;

    stages[0] = (VkPipelineShaderStageCreateInfo){
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert,
        .pName  = "main",
    };
    stages[1] = (VkPipelineShaderStageCreateInfo){
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag,
        .pName  = "main",
    };

    if (vkCreateGraphicsPipelines(s->ctx->device, VK_NULL_HANDLE,
                                  1, &pci, NULL,
                                  &s->solid_rect_pipeline) != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateGraphicsPipelines failed");
        goto fail;
    }

    vkDestroyShaderModule(s->ctx->device, frag, NULL);
    vkDestroyShaderModule(s->ctx->device, vert, NULL);
    return true;

fail:
    if (frag) vkDestroyShaderModule(s->ctx->device, frag, NULL);
    if (vert) vkDestroyShaderModule(s->ctx->device, vert, NULL);
    if (s->solid_rect_layout) {
        vkDestroyPipelineLayout(s->ctx->device, s->solid_rect_layout, NULL);
        s->solid_rect_layout = VK_NULL_HANDLE;
    }
    return false;
}

bool fx_make_images(fx_surface *s)
{
    vkGetSwapchainImagesKHR(s->ctx->device, s->swapchain, &s->image_count, NULL);
    if (s->image_count > FX_MAX_SWAPCHAIN_IMAGES) {
        FX_LOGE(s->ctx, "swapchain image count %u exceeds cap %u",
                s->image_count, FX_MAX_SWAPCHAIN_IMAGES);
        return false;
    }
    VkImage images[FX_MAX_SWAPCHAIN_IMAGES];
    vkGetSwapchainImagesKHR(s->ctx->device, s->swapchain, &s->image_count,
                            images);

    for (uint32_t i = 0; i < s->image_count; ++i) {
        s->images[i].image = images[i];

        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = s->surface_format.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        if (vkCreateImageView(s->ctx->device, &vci, NULL,
                              &s->images[i].view) != VK_SUCCESS) {
            FX_LOGE(s->ctx, "vkCreateImageView failed");
            return false;
        }

        VkFramebufferCreateInfo fci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = s->render_pass,
            .attachmentCount = 1,
            .pAttachments    = &s->images[i].view,
            .width           = s->extent.width,
            .height          = s->extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(s->ctx->device, &fci, NULL,
                                &s->images[i].framebuffer) != VK_SUCCESS) {
            FX_LOGE(s->ctx, "vkCreateFramebuffer failed");
            return false;
        }
    }
    return true;
}

bool fx_make_frames(fx_surface *s)
{
    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = s->ctx->frame_cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VkSamplerCreateInfo sci_sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(s->ctx->device, &sci_sampler, NULL, &s->sampler) != VK_SUCCESS) return false;

    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkAllocateCommandBuffers(s->ctx->device, &ai,
                                     &s->frames[i].cmd) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(s->ctx->device, &sci, NULL,
                              &s->frames[i].image_available) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(s->ctx->device, &sci, NULL,
                              &s->frames[i].render_finished) != VK_SUCCESS) return false;
        if (vkCreateFence(s->ctx->device, &fci, NULL,
                          &s->frames[i].in_flight) != VK_SUCCESS) return false;

        fx_vbuf_pool_init(&s->frames[i].vbuf, s->ctx);

        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        };
        VkDescriptorPoolCreateInfo pci_desc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 64,
            .poolSizeCount = 1,
            .pPoolSizes = pool_sizes,
        };
        if (vkCreateDescriptorPool(s->ctx->device, &pci_desc, NULL, &s->frames[i].desc_pool) != VK_SUCCESS) return false;
    }
    return true;
}

bool fx_swapchain_build(fx_surface *s)
{
    VkPhysicalDevice phys = s->ctx->phys;
    VkDevice         dev  = s->ctx->device;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, s->vk_surface, &caps);

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, s->vk_surface, &nf, NULL);
    VkSurfaceFormatKHR *fmts = calloc(nf, sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, s->vk_surface, &nf, fmts);

    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, s->vk_surface, &np, NULL);
    VkPresentModeKHR *pms = calloc(np, sizeof(*pms));
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, s->vk_surface, &np, pms);

    s->surface_format = pick_format(fmts, nf);
    s->present_mode   = pick_present_mode(pms, np);
    s->extent         = clamp_extent(caps, s->requested_w, s->requested_h);

    free(fmts);
    free(pms);

    if (s->extent.width == 0 || s->extent.height == 0) {
        /* Minimized / hidden: skip building, retry on resize. */
        s->needs_recreate = true;
        return true;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;
    if (image_count > FX_MAX_SWAPCHAIN_IMAGES)
        image_count = FX_MAX_SWAPCHAIN_IMAGES;

    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = s->vk_surface,
        .minImageCount    = image_count,
        .imageFormat      = s->surface_format.format,
        .imageColorSpace  = s->surface_format.colorSpace,
        .imageExtent      = s->extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                          | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = s->present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };
    /* Some compositors don't support OPAQUE; pick the first supported. */
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkResult r = vkCreateSwapchainKHR(dev, &ci, NULL, &s->swapchain);
    if (r != VK_SUCCESS) {
        FX_LOGE(s->ctx, "vkCreateSwapchainKHR: %d", (int)r);
        return false;
    }

    if (!fx_make_render_pass(s, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)) return false;
    if (!fx_make_image_dsl(s)) return false;
    if (!fx_make_image_pipeline(s)) return false;
    if (!fx_make_text_pipeline(s)) return false;
    if (!fx_make_bootstrap_pipeline(s)) return false;
    if (!fx_make_images(s))      return false;
    if (!fx_make_frames(s))      return false;

    FX_LOGI(s->ctx, "swapchain %ux%u images=%u format=%d present=%d",
            s->extent.width, s->extent.height, s->image_count,
            (int)s->surface_format.format, (int)s->present_mode);
    s->needs_recreate = false;
    return true;
}

void fx_swapchain_destroy(fx_surface *s)
{
    VkDevice dev = s->ctx->device;

    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (s->frames[i].in_flight) {
            vkDestroyFence(dev, s->frames[i].in_flight, NULL);
            s->frames[i].in_flight = VK_NULL_HANDLE;
        }
        if (s->frames[i].image_available) {
            vkDestroySemaphore(dev, s->frames[i].image_available, NULL);
            s->frames[i].image_available = VK_NULL_HANDLE;
        }
        if (s->frames[i].render_finished) {
            vkDestroySemaphore(dev, s->frames[i].render_finished, NULL);
            s->frames[i].render_finished = VK_NULL_HANDLE;
        }
        if (s->frames[i].cmd) {
            vkFreeCommandBuffers(dev, s->ctx->frame_cmd_pool, 1,
                                 &s->frames[i].cmd);
            s->frames[i].cmd = VK_NULL_HANDLE;
        }
        fx_vbuf_pool_destroy(&s->frames[i].vbuf);
        fx_arena_destroy(&s->frames[i].arena);
        if (s->frames[i].desc_pool) {
            vkDestroyDescriptorPool(dev, s->frames[i].desc_pool, NULL);
            s->frames[i].desc_pool = VK_NULL_HANDLE;
        }
    }
    if (s->sampler) {
        vkDestroySampler(dev, s->sampler, NULL);
        s->sampler = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < s->image_count; ++i) {
        if (s->images[i].framebuffer) {
            vkDestroyFramebuffer(dev, s->images[i].framebuffer, NULL);
            s->images[i].framebuffer = VK_NULL_HANDLE;
        }
        if (s->images[i].view) {
            vkDestroyImageView(dev, s->images[i].view, NULL);
            s->images[i].view = VK_NULL_HANDLE;
        }
        s->images[i].image = VK_NULL_HANDLE;
    }
    s->image_count = 0;

    if (s->render_pass) {
        vkDestroyRenderPass(dev, s->render_pass, NULL);
        s->render_pass = VK_NULL_HANDLE;
    }
    if (s->solid_rect_pipeline) {
        vkDestroyPipeline(dev, s->solid_rect_pipeline, NULL);
        s->solid_rect_pipeline = VK_NULL_HANDLE;
    }
    if (s->solid_rect_layout) {
        vkDestroyPipelineLayout(dev, s->solid_rect_layout, NULL);
        s->solid_rect_layout = VK_NULL_HANDLE;
    }
    if (s->image_pipeline) {
        vkDestroyPipeline(dev, s->image_pipeline, NULL);
        s->image_pipeline = VK_NULL_HANDLE;
    }
    if (s->image_layout) {
        vkDestroyPipelineLayout(dev, s->image_layout, NULL);
        s->image_layout = VK_NULL_HANDLE;
    }
    if (s->text_pipeline) {
        vkDestroyPipeline(dev, s->text_pipeline, NULL);
        s->text_pipeline = VK_NULL_HANDLE;
    }
    if (s->text_layout) {
        vkDestroyPipelineLayout(dev, s->text_layout, NULL);
        s->text_layout = VK_NULL_HANDLE;
    }
    if (s->image_dsl) {
        vkDestroyDescriptorSetLayout(dev, s->image_dsl, NULL);
        s->image_dsl = VK_NULL_HANDLE;
    }
    if (s->swapchain) {
        vkDestroySwapchainKHR(dev, s->swapchain, NULL);
        s->swapchain = VK_NULL_HANDLE;
    }
}

void fx_surface_wait_idle(fx_surface *s)
{
    if (!s->ctx || !s->ctx->device) return;
    VkFence fences[FX_MAX_FRAMES_IN_FLIGHT];
    uint32_t n = 0;
    for (uint32_t i = 0; i < FX_MAX_FRAMES_IN_FLIGHT; ++i)
        if (s->frames[i].in_flight) fences[n++] = s->frames[i].in_flight;
    if (n) vkWaitForFences(s->ctx->device, n, fences, VK_TRUE, UINT64_MAX);
}
