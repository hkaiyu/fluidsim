#include "pipeline.h"
#include "context.h"
#include "utils.h"
#include "bindless.h"

Pipeline
CreateGraphicsPipeline(const GraphicsPipelineInfo& info)
{
    VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT
        }
    };
    return CreateGraphicsPipeline(info, colorBlendAttachments, ARRAY_SIZE(colorBlendAttachments));
}


Pipeline
CreateGraphicsPipeline(const GraphicsPipelineInfo& info,
                       const VkPipelineColorBlendAttachmentState *pBlendAttachments,
                       const u32 attachmentCount)
{
    const VkDevice device = VulkanGetDevice();
    const VkPipelineLayout pipelineLayout = GetBindlessPipelineLayout();

    VkDynamicState dynamicStates[] = {
        // viewport state
        VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
        VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        // depth stencil state
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP,
        // rasterization state
        VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        // color blend state
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    };

    VkPipelineVertexInputStateCreateInfo nullVertexInputState =
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    // all fields static
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = info.topology,
        .primitiveRestartEnable = VK_FALSE
    };
    VkPipelineViewportStateCreateInfo nullViewportState =
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

    VkPipelineRasterizationStateCreateInfo rasterizationState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,                   // baked in
        .rasterizerDiscardEnable = VK_FALSE,            // baked in
        .polygonMode = info.polygonMode,                // baked in
        .cullMode = 0,                                  // dynamically set
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,   // dynamically set
        .depthBiasEnable = VK_FALSE,                    // dynamically set
        .depthBiasConstantFactor = 0.0f,                // dynamically set
        .depthBiasClamp = 0.0f,                         // dynamically set
        .depthBiasSlopeFactor = 0.0f,                   // dynamically set
        .lineWidth = 1.0f                               // dynamically set
    };

    // all fields static
    VkPipelineMultisampleStateCreateInfo multisampleState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineDepthStencilStateCreateInfo depthStencilState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,             // dynamically set
        .depthWriteEnable = VK_TRUE,            // dynamically set
        .depthCompareOp = VK_COMPARE_OP_NEVER,  // dynamically set
        .depthBoundsTestEnable = VK_FALSE,      // no bounds test
        .front = {},                            // dynamically set
        .back = {},                             // dynamically set
        .minDepthBounds = 0.0f,                 // not used
        .maxDepthBounds = 1.0f                  // not used
    };

    VkPipelineColorBlendStateCreateInfo colorBlendState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = attachmentCount,
        .pAttachments = pBlendAttachments,
    };

    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = ARRAY_SIZE(dynamicStates),
        .pDynamicStates = &dynamicStates[0]
    };

    // TODO: caller needs to set these; for now, we hardcode format to swapchain format
    VkFormat format = VulkanGetSwapchainFormat();
    VkPipelineRenderingCreateInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT
    };

    // ========= Shader stuff =========
    VkShaderModuleCreateInfo vertexShader {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = info.vert.size,
        .pCode = info.vert.pCode
    };

    VkShaderModuleCreateInfo fragmentShader {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = info.frag.size,
        .pCode = info.frag.pCode,
    };

    u32 numStages = 1;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = &vertexShader;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = VK_NULL_HANDLE;
    shaderStages[0].pName = info.vert.entry;
    shaderStages[0].pSpecializationInfo = nullptr;

    if (info.frag.pCode) [[likely]]
    {
        numStages = 2;
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].pNext = &fragmentShader;
        shaderStages[1].flags = 0;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = VK_NULL_HANDLE;
        shaderStages[1].pName = info.frag.entry;
        shaderStages[1].pSpecializationInfo = nullptr;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stageCount = numStages,
        .pStages = &shaderStages[0],
        .pVertexInputState = &nullVertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pTessellationState = nullptr,
        .pViewportState = &nullViewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    return Pipeline { pipeline };
}

Pipeline
CreateComputePipeline(const ComputePipelineInfo& info)
{
    const VkDevice device = VulkanGetDevice();
    VkShaderModuleCreateInfo shaderInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .codeSize = info.comp.size,
        .pCode = info.comp.pCode
    };

    VkPipelineShaderStageCreateInfo stageInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = &shaderInfo,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .pName = info.comp.entry
    };

    VkComputePipelineCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &shaderInfo,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .pName = info.comp.entry
        },
        .layout = GetBindlessPipelineLayout(),
    };

    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
    return Pipeline { pipeline };
}

void
DestroyPipeline(Pipeline pipeline)
{
    vkDestroyPipeline(VulkanGetDevice(), pipeline.handle, nullptr);
}
