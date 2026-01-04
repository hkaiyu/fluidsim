#pragma once

#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <stdint.h>

#include "../core/arena.h"

enum BindlessType
{
    BINDLESS_TYPE_SAMPLER = 0,
    BINDLESS_TYPE_TEXTURE = 1,
    BINDLESS_TYPE_RW_TEXTURE = 2,
    BINDLESS_TYPE_COUNT
};

struct DescriptorBuffer
{
    VkBuffer        handle;
    VmaAllocation   allocation;
    void*           pMappedData;
    VkDeviceAddress address;
    uint32_t        offset; // offset into VkBuffer for the actual descriptor buffer... we will prob set to 0
};

struct DescriptorArrayInfo
{
    VkDescriptorType type;
    VkDeviceSize     offset;
    uint32_t         stride;
    uint32_t         count;
    bool*            slots; // bool[count]
};

struct BindlessContext
{
    Arena*                  arena;
    DescriptorBuffer        descriptorBuffer;
    DescriptorArrayInfo     descriptorArrays[BINDLESS_TYPE_COUNT];
    VkDescriptorSetLayout   descriptorSetLayout;
    VkPipelineLayout        pipelineLayout;
};

void                       BindlessInit();
const DescriptorBuffer&    GetDescriptorBuffer();
VkPipelineLayout           GetBindlessPipelineLayout();
VkDescriptorSetLayout      GetBindlessSetLayout();
const DescriptorArrayInfo& GetDescriptorArrayInfo(BindlessType type);
uint32_t                   AllocateBindlessSlot(BindlessType type);
void                       FreeBindlessSlot(BindlessType type, uint32_t slot);
void                       WriteDescriptor(BindlessType t, uint32_t slot, VkDescriptorDataEXT data);
