#pragma once

#include <vulkan/vulkan.h>

#include "shaders.h"

struct GraphicsPipelineAttachmentInfo
{
    // TODO: graphics pipeline needs to add per-attachment info too
    // - blend state
    // - format
};

struct GraphicsPipelineInfo
{
    CompiledSpirvShader vert{};
    CompiledSpirvShader frag{};
    VkPolygonMode polygonMode    = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkFormat format              = VK_FORMAT_UNDEFINED;
};

struct ComputePipelineInfo
{
    CompiledSpirvShader comp{};
};

struct Pipeline
{
    VkPipeline handle = VK_NULL_HANDLE;
};

Pipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info);
Pipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info,
                                const VkPipelineColorBlendAttachmentState *pBlendAttachments,
                                const uint32_t attachmentCount);

Pipeline CreateComputePipeline(const ComputePipelineInfo& info);

void DestroyPipeline(Pipeline pipeline);
