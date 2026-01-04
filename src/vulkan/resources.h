#pragma once

#include "../core/utils.h"
#include <vma/vk_mem_alloc.h>

struct Buffer
{
    VkBuffer                handle;
    VkDeviceAddress         gpuAddress;
    VkPipelineStageFlags2   lastStage;
    VkAccessFlags2          lastAccess;
    VmaAllocation           allocation;
    void*                   pMapped;
};

struct Image
{
    VkImage                 handle;
    VkImageView             imageView;
    VkFormat                format;
    VkExtent3D              extent;
    VkImageLayout           lastLayout;
    VkPipelineStageFlags2   lastStage;
    VkAccessFlags2          lastAccess;
    VmaAllocation           allocation;
    u32                     bindlessSlotStorage;
    u32                     bindlessSlotSample;
};

struct Sampler
{
    VkSampler   handle;
    uint32_t    bindlessSlot;
};

struct ImageCreateInfo
{
    VkImageType imageType;
    VkExtent3D extent;
    VkFormat format;

    bool isAttachment = false;
    bool isStorage = false;
    bool isSampled = false;

    u32 mipLevels   = 1;
    u32 arrayLayers = 1;
};

Buffer CreateBuffer(size_t size, VkBufferUsageFlags usage, bool persistent = false);

Sampler CreateSampler(VkFilter magFilter,
                      VkFilter minFilter,
                      VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                      bool enableAnisotropy = true);

Image CreateImage(const ImageCreateInfo& info);

Image CreateDepthAttachment(u32 width,
                            u32 height,
                            VkFormat format = VK_FORMAT_D32_SFLOAT,
                            bool isSampled = false);

void DestroyBuffer(Buffer* buffer);
void DestroySampler(Sampler* sampler);
void DestroyImage(Image* image);

Image UploadTexture2D(void* data,
                      size_t size,
                      VkExtent2D textureExtent,
                      VkFormat textureFormat,
                      VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
