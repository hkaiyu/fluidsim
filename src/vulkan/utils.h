#pragma once

#include <vulkan/vulkan.h>
#include "../core/utils.h"

VkImageAspectFlags InferAspectFromFormat(VkFormat format);
VkImageUsageFlags InferAttachmentFromFormat(VkFormat format);
const char* VkResultToString(VkResult result);

inline uint64_t
AlignedSize(uint64_t size, uint64_t alignment)
{ 
    return (size + alignment - 1) & ~(alignment - 1);
}

#ifdef NDEBUG
#define VK_CHECK(expr) expr
#else
#define VK_CHECK(expr) do { const VkResult err = expr; if (err) [[unlikely]] { WARN("\n%s received from %s at %s:%d\n", VkResultToString(err), expr, __FILE__, __LINE__); assert(false); }} while(0)
#endif


