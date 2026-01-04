#include "bindless.h"
#include "utils.h"
#include "context.h"

#include <glm/packing.hpp>

namespace {

BindlessContext gBindless;

// https://shader-slang.org/slang/user-guide/convenience-features.html#descriptorhandle-for-bindless-descriptor-access
VkDescriptorSetLayoutBinding gBindings[BINDLESS_TYPE_COUNT] = {
    {
        // Samplers
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 8,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT
    },
    {
        // Textures
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = 32,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT
    },
    {
        // Storage images
        .binding = 2,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 32,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT
    },
};

}

internal uint32_t
DescriptorStride(const VkPhysicalDeviceDescriptorBufferPropertiesEXT& p, VkDescriptorType t)
{
    switch (t)
    {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return p.samplerDescriptorSize;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return p.combinedImageSamplerDescriptorSize;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return p.sampledImageDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return p.storageImageDescriptorSize;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return p.uniformTexelBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return p.storageTexelBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return p.uniformBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return p.storageBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return p.inputAttachmentDescriptorSize;
        default: return 0;
    }
}

internal VkDescriptorSetLayout
CreateDescriptorSetLayout(VkDevice device)
{
    VkDescriptorBindingFlags bindFlags[BINDLESS_TYPE_COUNT] = { VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT };
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = BINDLESS_TYPE_COUNT,
        .pBindingFlags = bindFlags,
    };
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = &flagsInfo,
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    info.bindingCount = ARRAY_SIZE(gBindings);
    info.pBindings = gBindings;

    VkDescriptorSetLayout setLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &setLayout));
    return setLayout;
}

internal VkPipelineLayout
CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout layout)
{
    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_ALL ,
        .offset = 0,
        .size = 256 // guaranteed size for push constants in Vulkan 1.4
    };

    VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));
    return pipelineLayout;
}

internal DescriptorBuffer
CreateDescriptorBuffer(VkDevice device, VmaAllocator allocator, uint64_t alignedSize)
{
    DescriptorBuffer buffer;
    VkBufferCreateInfo binfo{};
    binfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    binfo.size = alignedSize;
    binfo.usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                  VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    binfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ainfo{};
    ainfo.usage = VMA_MEMORY_USAGE_AUTO;
    ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo got{};
    VK_CHECK(vmaCreateBuffer(allocator,
                             &binfo,
                             &ainfo,
                             &buffer.handle,
                             &buffer.allocation,
                             &got));
    buffer.pMappedData = got.pMappedData;

    VkBufferDeviceAddressInfo addr{};
    addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addr.buffer = buffer.handle;
    buffer.address = vkGetBufferDeviceAddress(device, &addr);
    buffer.offset = 0;

    return buffer;
}

void
BindlessInit()
{
    const VkDevice device = VulkanGetDevice();
    const VmaAllocator allocator = VulkanGetAllocator();

    gBindless.arena = ArenaAlloc(KB(2));

    VkDescriptorSetLayout setLayout = CreateDescriptorSetLayout(device);
    VkPipelineLayout pipelineLayout = CreatePipelineLayout(device, setLayout);

    VkPhysicalDeviceDescriptorBufferPropertiesEXT dbProps {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT
    };

    VkPhysicalDeviceProperties2 props2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &dbProps
    };
    vkGetPhysicalDeviceProperties2(VulkanGetPhysicalDevice(), &props2);

    VkDeviceSize layoutSize;
    vkGetDescriptorSetLayoutSizeEXT(device, setLayout, &layoutSize);
    VkDeviceSize totalSize = AlignedSize(layoutSize, dbProps.descriptorBufferOffsetAlignment);

    for (uint32_t i = 0; i < BINDLESS_TYPE_COUNT; i++)
    {
        VkDeviceSize offset;
        vkGetDescriptorSetLayoutBindingOffsetEXT(device, setLayout, gBindings[i].binding, &offset);
        gBindless.descriptorArrays[i].type   = gBindings[i].descriptorType;
        gBindless.descriptorArrays[i].offset = offset;
        gBindless.descriptorArrays[i].stride = DescriptorStride(dbProps, gBindings[i].descriptorType);
        gBindless.descriptorArrays[i].count  = gBindings[i].descriptorCount;
        gBindless.descriptorArrays[i].slots = ArenaPushArrayZero(gBindless.arena, bool, gBindings[i].descriptorCount);
    }

    gBindless.descriptorBuffer = CreateDescriptorBuffer(device, allocator, totalSize);
    gBindless.descriptorSetLayout = setLayout;
    gBindless.pipelineLayout = pipelineLayout;

}

const DescriptorBuffer&
GetDescriptorBuffer()
{
    return gBindless.descriptorBuffer;
}

const DescriptorArrayInfo&
GetDescriptorArrayInfo(BindlessType t)
{
    return gBindless.descriptorArrays[t];
}

VkDescriptorSetLayout
GetBindlessSetLayout()
{
    return gBindless.descriptorSetLayout;
}

VkPipelineLayout
GetBindlessPipelineLayout()
{
    return gBindless.pipelineLayout;
}

uint32_t
AllocateBindlessSlot(BindlessType t)
{
    DescriptorArrayInfo& descArr = gBindless.descriptorArrays[t];
    for (uint32_t i = 0; i < descArr.count; i++)
    {
        if (!descArr.slots[i])
        {
            descArr.slots[i] = true;
            return i;
        }
    }
    assert(false);
    return 0;
}

void
FreeBindlessSlot(BindlessType t, uint32_t slot)
{
    DescriptorArrayInfo& descArr = gBindless.descriptorArrays[t];
    assert(slot < descArr.count);
    assert(descArr.slots[slot]);
    descArr.slots[slot] = false;
}

void
WriteDescriptor(BindlessType t, uint32_t slot, VkDescriptorDataEXT data)
{
    const VkDevice device = VulkanGetDevice();
    const DescriptorBuffer& descBuffer = GetDescriptorBuffer();
    const DescriptorArrayInfo& descArr = GetDescriptorArrayInfo(t);

    VkDescriptorGetInfoEXT getInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
        .type  = descArr.type,
        .data = data
    };

    vkGetDescriptorEXT(device, &getInfo, sizeof(descArr.stride),
                       (u8*) descBuffer.pMappedData + descBuffer.offset + descArr.offset + (slot * descArr.stride));
}

