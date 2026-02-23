#include "commands.h"
#include "../core/threads.h"
#include "../core/arena.h"
#include "utils.h"
#include "bindless.h"

// // We will build up dependencies into this dependency sink, then flush it on commands that use the dependency
// // For example, let's say a user create commands to transition a buffer and image before using them both in a graphics
// // pass: when they call the commands, the dependencies will be stored in the dependency sink, but no barriers will be
// // emitted. Once the user calls a command that uses the transitioned resources such as CmdDraw, the dependency sink will
// // flush and then emit all the appropriate barriers in a batched fashion with vkCmdPipelineBarrier2. In rendering code,
// // we simply just can specify that the some resource needs to be transitioned and treat it as if it immediately 
// // transitions the resource, but in the backend, we get the performance benefits of batching dependencies in the 
// // pipeline barriers. The dependency sink is also not thread-safe, we probably wouldn't get much benefit from making it
// // thread-safe for now.
// struct DependencySink
// {
//     static constexpr u32    MAX_DEPENDENCIES = 32;
//     u32                     bufferBarrierCount;
//     VkBufferMemoryBarrier2  bufferBarriers[MAX_DEPENDENCIES];
//     u32                     imageBarrierCount;
//     VkImageMemoryBarrier2   imageBarriers[MAX_DEPENDENCIES];
// };

// global DependencySink sink = {};
//
// TODO: convert. this type of design may be easier to read
// CmdBeginRenderPass({
//     .imageUsages = {
//         { frame->swapchain, RESOURCE_USAGE },
//     }
//     .bufferUsages = {
//         { }
//     },
// });
// ... 
// CmdEndRenderPass();
// // and also
// CmdBeginComputePass({
//     .imageUsages = ...
//     .bufferUsages = ...
// });
// ...
// CmdEndComputePass();

void
CmdBeginRendering(VkCommandBuffer cmd, const RenderInfo& renderInfo)
{
    ArenaTemp scratch = ScratchBegin();
    u32 numColorAttachments = (u32) renderInfo.colorAttachments.size();
    VkRenderingAttachmentInfo *colorAttachments
        = ArenaPushArrayZero(scratch.arena, VkRenderingAttachmentInfo, numColorAttachments);

    for (u32 i = 0; i < numColorAttachments; i++)
    {
        const RenderAttachmentInfo* attach = renderInfo.colorAttachments.begin() + i;
        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = attach->image->imageView;
        colorAttachments[i].imageLayout = attach->image->lastLayout;
        colorAttachments[i].loadOp = attach->loadOp;
        colorAttachments[i].storeOp = attach->storeOp;
        colorAttachments[i].clearValue = attach->clearValue;
    }

    auto IsRectZero = [](const VkRect2D& rect) {
        static_assert(sizeof(VkRect2D) == 16);
        const uint64_t* p = (const uint64_t*) &rect;
        return (p[0] | p[1]) == 0; // checks if both extent and offset are completely zero
    };

    VkRect2D renderArea = renderInfo.renderArea;
    if (IsRectZero(renderArea)) // user did not provide a renderArea -> use image's full extent
    {
        const RenderAttachmentInfo* attach = renderInfo.colorAttachments.begin();
        renderArea = {{0, 0}, {attach->image->extent.width, attach->image->extent.height}};
    }
    else
    {
        renderArea = renderInfo.renderArea;
    }

    VkRenderingInfo info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = renderInfo.layerCount,
        .colorAttachmentCount = numColorAttachments,
        .pColorAttachments = colorAttachments
    };

    VkRenderingAttachmentInfo depthAttachment { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    if (renderInfo.depthAttachment.image)
    {
        const RenderAttachmentInfo& attach = renderInfo.depthAttachment;
        depthAttachment.imageView = attach.image->imageView;
        depthAttachment.imageLayout = attach.image->lastLayout;
        depthAttachment.loadOp = attach.loadOp;
        depthAttachment.storeOp = attach.storeOp;
        depthAttachment.clearValue = attach.clearValue;

        info.pDepthAttachment = &depthAttachment;
    }

    vkCmdBeginRendering(cmd, &info);

    ScratchEnd(scratch);
}

void
CmdEndRendering(VkCommandBuffer cmd)
{
    vkCmdEndRendering(cmd);
}

void
CmdPipelineBarrier(VkCommandBuffer cmd, const DependencyInfo& info)
{
    ArenaTemp scratch = ScratchBegin();

    VkMemoryBarrier2* memoryBarriers =
        ArenaPushArrayZero(scratch.arena, VkMemoryBarrier2, info.memoryDependencyCount);
    for (u32 i = 0; i < info.memoryDependencyCount; i++)
    {
        const MemoryDependency& dep = info.pMemoryDependencies[i];

        memoryBarriers[i].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarriers[i].srcStageMask = dep.srcStage;
        memoryBarriers[i].srcAccessMask = dep.srcAccess;
        memoryBarriers[i].dstStageMask = dep.dstStage;
        memoryBarriers[i].dstAccessMask = dep.dstAccess;
    }

    VkBufferMemoryBarrier2* bufferBarriers =
        ArenaPushArrayZero(scratch.arena, VkBufferMemoryBarrier2, info.bufferDependencyCount);

    for (u32 i = 0; i < info.bufferDependencyCount; i++)
    {
        const BufferDependency& dep = info.pBufferDependencies[i];
        bufferBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        // TODO:
    }

    VkImageMemoryBarrier2* imageBarriers =
        ArenaPushArrayZero(scratch.arena, VkImageMemoryBarrier2, info.imageDependencyCount);

    for (u32 i = 0; i < info.imageDependencyCount; i++)
    {
        const ImageDependency& dep = info.pImageDependencies[i];

        // fill out struct
        imageBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarriers[i].srcStageMask = dep.image->lastStage;
        imageBarriers[i].dstStageMask = dep.stage;
        imageBarriers[i].srcAccessMask = dep.image->lastAccess;
        imageBarriers[i].dstAccessMask = dep.access;
        imageBarriers[i].oldLayout = dep.image->lastLayout;
        imageBarriers[i].newLayout = dep.layout;
        imageBarriers[i].image = dep.image->handle;
        imageBarriers[i].subresourceRange.aspectMask = InferAspectFromFormat(dep.image->format);
        imageBarriers[i].subresourceRange.baseMipLevel = 0;
        imageBarriers[i].subresourceRange.levelCount = 1;
        imageBarriers[i].subresourceRange.baseArrayLayer = 0;
        imageBarriers[i].subresourceRange.layerCount = 1;

        // update image
        dep.image->lastLayout = dep.layout;
        dep.image->lastStage = dep.stage;
        dep.image->lastAccess = dep.access;
    }

    const VkDependencyInfo depInfo {
        .sType                      = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount         = info.memoryDependencyCount,
        .pMemoryBarriers            = memoryBarriers,
        .bufferMemoryBarrierCount   = info.bufferDependencyCount,
        .pBufferMemoryBarriers      = bufferBarriers,
        .imageMemoryBarrierCount    = info.imageDependencyCount,
        .pImageMemoryBarriers       = imageBarriers
    };

    vkCmdPipelineBarrier2(cmd, &depInfo);
    ScratchEnd(scratch);
}

void
CmdPushConstants(VkCommandBuffer cmd, u32 offset, u32 size, void* constants)
{
    VkPipelineLayout layout = GetBindlessPipelineLayout();
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_ALL, offset, size, constants);
}

void
CmdClearColorImage(VkCommandBuffer cmd, Image* image, const VkClearColorValue* color)
{
    const VkImageSubresourceRange range { InferAspectFromFormat(image->format), 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, image->handle, image->lastLayout, color, 1, &range);
}

void
CmdBindComputePipeline(VkCommandBuffer cmd, Pipeline pipeline)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
}

void
CmdBindGraphicsPipeline(VkCommandBuffer cmd, Pipeline pipeline)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
}

void
CmdDraw(VkCommandBuffer cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    vkCmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void
CmdDispatch(VkCommandBuffer cmd, u32 groupsX, u32 groupsY, u32 groupsZ)
{
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}

void
CmdFillBuffer(VkCommandBuffer cmd, Buffer* buffer, u32 data)
{
    vkCmdFillBuffer(cmd, buffer->handle, 0, VK_WHOLE_SIZE, data);
}

void
CmdBindBindlessDescriptors(VkCommandBuffer cmd)
{
    const DescriptorBuffer& buffer = GetDescriptorBuffer();
    VkDescriptorBufferBindingInfoEXT bindInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .address = buffer.address,
        .usage = VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
    };
    vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindInfo);

    u32 bufferIndices[] = {0};
    VkDeviceSize offsets[] = {buffer.offset};
    vkCmdSetDescriptorBufferOffsetsEXT(cmd,
                                       VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       GetBindlessPipelineLayout(),
                                       0,
                                       1,
                                       bufferIndices,
                                       offsets
                                       );

    vkCmdSetDescriptorBufferOffsetsEXT(cmd,
                                       VK_PIPELINE_BIND_POINT_COMPUTE,
                                       GetBindlessPipelineLayout(),
                                       0,
                                       1,
                                       bufferIndices,
                                       offsets
                                       );

}

void CmdBlitImage(VkCommandBuffer cmd, Image* src, Image* dst, VkFilter filter)
{
    const VkImageSubresourceLayers subresourceSrc {
        .aspectMask = InferAspectFromFormat(src->format),
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    const VkImageSubresourceLayers subresourceDst {
        .aspectMask = InferAspectFromFormat(dst->format),
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    // assumes 2d blit for now
    const VkImageBlit region {
        .srcSubresource = subresourceSrc,
        .srcOffsets = { { 0, 0, 0 }, { (s32) src->extent.width, (s32) src->extent.height, 1 } },
        .dstSubresource = subresourceDst,
        .dstOffsets = { { 0, 0, 0 }, { (s32) dst->extent.width, (s32) dst->extent.height, 1 } },
    };

    vkCmdBlitImage(cmd, src->handle, src->lastLayout, dst->handle, dst->lastLayout, 1, &region, filter);
}
