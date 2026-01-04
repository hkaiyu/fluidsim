#include "resources.h"

#include "utils.h"
#include "context.h"
#include "commands.h"
#include "immediate_commands.h"
#include "bindless.h"

#include <vma/vk_mem_alloc.h>
#include <string.h>

internal VkImageViewType
ImageTypeToViewType(VkImageType imageType,
                    uint32_t arrayLayers,
                    VkImageCreateFlags createFlags)
{
    switch (imageType)
    {
    case VK_IMAGE_TYPE_1D:
        return (arrayLayers > 1)
            ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
            : VK_IMAGE_VIEW_TYPE_1D;

    case VK_IMAGE_TYPE_2D:
        if (createFlags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
        {
            // Layers must be multiple of 6 for cube compatibility
            if (arrayLayers == 6)
                return VK_IMAGE_VIEW_TYPE_CUBE;
            else if (arrayLayers > 6 && (arrayLayers % 6) == 0)
                return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            // Fallback: just treat as a 2D array
            else
                return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }
        else
        {
            return (arrayLayers > 1)
                ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                : VK_IMAGE_VIEW_TYPE_2D;
        }

    case VK_IMAGE_TYPE_3D:
        return VK_IMAGE_VIEW_TYPE_3D;

    default:
        assert(false);
        return VK_IMAGE_VIEW_TYPE_2D;
    }
}

Buffer
CreateBuffer(size_t size, VkBufferUsageFlags usage, bool persistent)
{
    const VkDevice device = VulkanGetDevice();
    const VmaAllocator alloc = VulkanGetAllocator();
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (persistent)
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

    Buffer buffer{};
    VmaAllocationInfo allocInfo{};
    VK_CHECK(vmaCreateBuffer(alloc, &bufferInfo, &allocCreateInfo, &buffer.handle, &buffer.allocation, &allocInfo));

    const VkBufferDeviceAddressInfo bdaInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.handle
    };
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        buffer.gpuAddress = vkGetBufferDeviceAddress(device, &bdaInfo);
    buffer.pMapped = allocInfo.pMappedData;
    return buffer;
}

Sampler
CreateSampler(VkFilter magFilter,
              VkFilter minFilter,
              VkSamplerAddressMode addressMode,
              bool enableAnisotropy)
{
    const VkDevice device = VulkanGetDevice();
    float maxAnisotropy = 0.0f;
    if (enableAnisotropy)
    {
        const VkPhysicalDeviceProperties* properties = VulkanGetPhysicalDeviceProperties();
        maxAnisotropy = properties->limits.maxSamplerAnisotropy;
    }

    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = magFilter,
        .minFilter = minFilter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        .anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = maxAnisotropy,
        .compareEnable = VK_FALSE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    Sampler sampler;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, 0, &sampler.handle));

    sampler.bindlessSlot = AllocateBindlessSlot(BINDLESS_TYPE_SAMPLER);

    VkDescriptorDataEXT data {.pSampler = &sampler.handle };
    WriteDescriptor(BINDLESS_TYPE_SAMPLER, sampler.bindlessSlot, data);

    return sampler;
}

Image
CreateImage(const ImageCreateInfo& info)
{
    const VkDevice device = VulkanGetDevice();
    const VmaAllocator allocator = VulkanGetAllocator();

    VkExtent3D extent = {
        MAX(1, info.extent.width),
        MAX(1, info.extent.height),
        MAX(1, info.extent.depth)
    };

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = info.imageType,
        .format = info.format,
        .extent = extent,
        .mipLevels = info.mipLevels,
        .arrayLayers = info.arrayLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    imageInfo.usage |= info.isAttachment ? InferAttachmentFromFormat(info.format) : 0;
    imageInfo.usage |= info.isStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
    imageInfo.usage |= info.isSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;

    VmaAllocationCreateInfo alloc {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .priority = 1.0f
    };

    // NOTE: We could infer bytes per pixel from format, but this is good enough heuristic for now
    constexpr u64 threshold = 2048 * 2048;
    const u64 pixels = (u64) extent.width * extent.height * extent.depth;

    // If texture is large, we tell vma to create a dedicated memory block for it
    if (pixels >= threshold)
    {
        alloc.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    Image image{};
    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &alloc, &image.handle, &image.allocation, nullptr));

    VkImageViewCreateInfo imageViewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.handle,
        .viewType = ImageTypeToViewType(imageInfo.imageType, imageInfo.arrayLayers, imageInfo.flags),
        .format = info.format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = info.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = info.arrayLayers
        }
    };

    VK_CHECK(vkCreateImageView(device, &imageViewInfo, nullptr, &image.imageView));

    image.format = info.format;
    image.extent = extent;
    image.lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkDescriptorImageInfo imageDescInfo {
        .imageView = image.imageView
    };

    if (info.isStorage)
    {
        const VkDescriptorDataEXT data { .pStorageImage = &imageDescInfo };
        image.bindlessSlotStorage = AllocateBindlessSlot(BINDLESS_TYPE_RW_TEXTURE);
        // INFO("    Allocated storage slot = %u", image.bindlessSlotStorage);
        WriteDescriptor(BINDLESS_TYPE_RW_TEXTURE, image.bindlessSlotStorage, data);
    }

    if (info.isSampled)
    {
        const VkDescriptorDataEXT data { .pSampledImage = &imageDescInfo };
        image.bindlessSlotSample = AllocateBindlessSlot(BINDLESS_TYPE_TEXTURE);
        // INFO("    Allocated sampled slot = %u", image.bindlessSlotSample);
        WriteDescriptor(BINDLESS_TYPE_TEXTURE, image.bindlessSlotSample, data);
    }

    return image;
}

Image
CreateDepthAttachment(u32 width, u32 height, VkFormat format, bool isSampled)
{
    const VkDevice device = VulkanGetDevice();
    const VmaAllocator allocator = VulkanGetAllocator();

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo alloc {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .priority = 1.0f
    };

    Image image{};
    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &alloc, &image.handle, &image.allocation, nullptr));

    VkImageViewCreateInfo imageViewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = 
        {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY 
        },
        .subresourceRange = 
        {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VK_CHECK(vkCreateImageView(device, &imageViewInfo, nullptr, &image.imageView));

    image.format = format;
    image.extent = {width, height, 1};
    image.lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    return image;
}

void 
DestroyBuffer(Buffer* buffer)
{
    const VmaAllocator allocator = VulkanGetAllocator();
    vmaDestroyBuffer(allocator, buffer->handle, buffer->allocation);
    *buffer = {};
}

void
DestroyImage(Image* image)
{
    const VmaAllocator allocator = VulkanGetAllocator();

    if (image->bindlessSlotStorage)
        FreeBindlessSlot(BINDLESS_TYPE_RW_TEXTURE, image->bindlessSlotStorage);
    if (image->bindlessSlotSample)
        FreeBindlessSlot(BINDLESS_TYPE_TEXTURE, image->bindlessSlotSample);

    vmaDestroyImage(allocator, image->handle, image->allocation);
    *image = {};
}

Image
UploadTexture2D(void* data, size_t size, VkExtent2D textureExtent, VkFormat textureFormat, VkPipelineStageFlags2 stages)
{
    if (!data) return Image{};

    const VmaAllocator allocator = VulkanGetAllocator();

    // 1. Create mapped buffer
    Buffer buffer = CreateBuffer(size,
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    void *pMapped;
    vmaMapMemory(allocator, buffer.allocation, &pMapped);
    memcpy(pMapped, data, size);
    vmaUnmapMemory(allocator, buffer.allocation);

    // 2. Create image to copy to
    Image image = CreateImage({
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {textureExtent.width, textureExtent.height, 1},
        .format = textureFormat,
        .isSampled = true
    });

    VkCommandBuffer cmd = BeginImmediateCommands();

    // 3. Transition image layout (prepare image to be transferred to)
    ImageDependency transferDst {
        .image = &image,
        .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .stage = VK_PIPELINE_STAGE_2_COPY_BIT,
    };
    CmdPipelineBarrier(cmd, {
        .imageDependencyCount = 1,
        .pImageDependencies = &transferDst
    });

    // 4. Perform copy
    const VkBufferImageCopy copy {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { InferAspectFromFormat(textureFormat), 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = { textureExtent.width, textureExtent.height, 1},
    };
    vkCmdCopyBufferToImage(cmd, buffer.handle, image.handle, image.lastLayout, 1, &copy);

    // 5. Transition image layout to optimal read only
    ImageDependency optimalReadOnly {
        .image = &image,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .access = VK_ACCESS_2_SHADER_READ_BIT,
        .stage = stages,
    };
    CmdPipelineBarrier(cmd, {
        .imageDependencyCount = 1,
        .pImageDependencies = &optimalReadOnly
    });

    // 6. Clean up
    SubmitImmediateCommands(cmd);
    DestroyBuffer(&buffer);
    return image;
}

