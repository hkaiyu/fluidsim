#include "context.h"
#include "utils.h"

VkCommandBuffer
BeginImmediateCommands()
{
    const VkDevice device = VulkanGetDevice();
    const VkCommandBufferAllocateInfo cmdBufAllocInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = VulkanGetTransientPool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &cmd));
    const VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    return cmd;
}

void
SubmitImmediateCommands(VkCommandBuffer cmd)
{
    const VkCommandBufferSubmitInfo cmdSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd
    };
    const VkSubmitInfo2 submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdSubmitInfo
    };

    const VkDevice device = VulkanGetDevice();
    const VkQueue queue = VulkanGetQueue();
    VK_CHECK(vkEndCommandBuffer(cmd));
    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, VulkanGetTransientPool(), 1, &cmd);
}

