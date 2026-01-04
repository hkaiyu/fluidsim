#pragma once

#include "resources.h"
#include "pipeline.h"

#include <vulkan/vulkan.h>
#include <initializer_list>

struct MemoryDependency
{
    VkPipelineStageFlags2   srcStage;
    VkAccessFlags2          srcAccess;
    VkPipelineStageFlags2   dstStage;
    VkAccessFlags2          dstAccess;
};

struct BufferDependency
{
    Buffer  *buffer;
    // TODO:
};

struct ImageDependency
{
    Image*                  image;
    VkImageLayout           layout;
    VkAccessFlags2          access;
    VkPipelineStageFlags2   stage;
};

struct DependencyInfo
{
    uint32_t            memoryDependencyCount = 0;
    MemoryDependency*   pMemoryDependencies   = nullptr;
    uint32_t            bufferDependencyCount = 0;
    BufferDependency*   pBufferDependencies   = nullptr;
    uint32_t            imageDependencyCount  = 0;
    ImageDependency*    pImageDependencies    = nullptr; 
};

struct RenderAttachmentInfo
{
    Image*              image = nullptr;
    VkAttachmentLoadOp  loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue        clearValue = {0, 0, 0, 0};
};

struct RenderInfo
{
    VkRect2D                                    renderArea = {0};
    uint32_t                                    layerCount = 1;
    std::initializer_list<RenderAttachmentInfo> colorAttachments;
    RenderAttachmentInfo                        depthAttachment = {};
};

void CmdBeginRendering(VkCommandBuffer cmd, const RenderInfo& renderInfo);
void CmdEndRendering(VkCommandBuffer cmd);
void CmdPipelineBarrier(VkCommandBuffer cmd, const DependencyInfo& info);
void CmdPushConstants(VkCommandBuffer cmd, u32 offset, u32 size, void* constants); // TODO: expose shader stage flags
void CmdClearColorImage(VkCommandBuffer cmd, Image* image, const VkClearColorValue* color);
void CmdFillBuffer(VkCommandBuffer cmd, Buffer* buffer, u32 data);
void CmdBindComputePipeline(VkCommandBuffer cmd, Pipeline pipeline);
void CmdBindGraphicsPipeline(VkCommandBuffer cmd, Pipeline pipeline);
void CmdDraw(VkCommandBuffer cmd, u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
void CmdDispatch(VkCommandBuffer cmd, u32 groupsX, u32 groupsY, u32 groupsZ);
void CmdBindBindlessDescriptors(VkCommandBuffer cmd);
void CmdBlitImage(VkCommandBuffer cmd, Image* src, Image* dst, VkFilter filter);
