#include "context.h"

#include "bindless.h"
#include "utils.h"
#include "commands.h"
#include "../core/utils.h"
#include "../core/threads.h"
#include "../config.h"

#include <GLFW/glfw3.h>
#include <string.h> // strcmp
#include <stdio.h>

struct GPUQueue
{
    static constexpr u32 INVALID_INDEX = ~0U;
    VkQueue handle  = VK_NULL_HANDLE;
    u32 familyIndex = INVALID_INDEX;
};

struct Swapchain
{
    VkSwapchainKHR  handle;
    VkFormat        format;
    u32             imageCount;
    u32             currentImage;
    Image           images[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore     presentReady[MAX_SWAPCHAIN_IMAGES];
    bool            resizeRequested;
};

struct PerFrameInfo
{
    u64             timelineWaitValue;
    VkSemaphore     imageAcquire;
    VkCommandPool   commandPool;
    VkCommandBuffer commandBuffer;
    Image           depthBuffer;
};

struct VulkanContext
{
    Arena*                      frameArena;
    GLFWwindow*                 window;
    VkInstance                  instance;
    VkDebugUtilsMessengerEXT    debugMessenger;
    VkSurfaceKHR                surface;
    VkPhysicalDevice            physicalDevice;
    GPUQueue                    queue;  // general queue for graphics, transfer, compute, present
    VkDevice                    device;
    VmaAllocator                allocator;
    VkCommandPool               transientPool; // for short-lived command buffers (like transfers)
    Swapchain                   swapchain;
    VkSemaphore                 timeline;
    PerFrameInfo                frames[MAX_FRAMES_IN_FLIGHT];
    u32                         currentFrame; // 0..MAX_FRAMES_IN_FLIGHT-1
};

// TODO: add code paths for mac/linux/extension+feature checks

internal VulkanContext* vk = nullptr;
internal VkPhysicalDeviceProperties physicalDeviceProps;
internal u64 g_timeline = MAX_FRAMES_IN_FLIGHT - 1;

internal VKAPI_ATTR VkBool32 VKAPI_CALL
DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    fprintf(stderr, "\n" COLOR("[Vulkan Validation Layer] ", COLOR_WARNING) "%s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

internal GPUQueue
FindQueue(VkPhysicalDevice gpu)
{
    ArenaTemp scratch = ScratchBegin();

    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(gpu, &familyCount, nullptr);

    VkQueueFamilyProperties2 *families = ArenaPushArrayZero(scratch.arena, VkQueueFamilyProperties2, familyCount);
    for (u32 i = 0; i < familyCount; i++) families[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;

    vkGetPhysicalDeviceQueueFamilyProperties2(gpu, &familyCount, families);

    auto supportsPresent = [&](VkPhysicalDevice gpu, VkSurfaceKHR surface, u32 familyIndex) -> bool
    {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, familyIndex, surface, &presentSupport);
        return presentSupport == VK_TRUE;
    };

    GPUQueue queue;
    for (u32 family = 0; family < familyCount; family++)
    {
        const auto& familyProps = families[family].queueFamilyProperties;
        VkQueueFlags flags = familyProps.queueFlags;
        if ((flags & VK_QUEUE_GRAPHICS_BIT) &&
            (flags & VK_QUEUE_COMPUTE_BIT) &&
            (flags & VK_QUEUE_TRANSFER_BIT) &&
            supportsPresent(gpu, vk->surface, family))
        {
            queue.familyIndex = family;
            break;
        }
    }

    ScratchEnd(scratch);
    return queue;
}

internal u64
ScorePhysicalDevice(VkPhysicalDevice gpu)
{
    // Query relevant device info
    VkPhysicalDeviceProperties2 props2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(gpu, &props2);

    const auto& props = props2.properties;

    u64 score = 0;
    // Necessary properties
    if (props.apiVersion >= MINIMUM_VULKAN_API_SUPPORTED)
    {
        // Weight 1: prefer discrete GPU
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 5000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 1000;

        // Weight 2: prefer newer Vulkan versions
        score += (u64) (VK_API_VERSION_MAJOR(props.apiVersion) * 100 + VK_API_VERSION_MINOR(props.apiVersion) * 10);
    }

    // INFO("GPU found: %s, Vulkan API version: %d.%d.%d", props.deviceName,
    //      VK_API_VERSION_MAJOR(props.apiVersion),
    //      VK_API_VERSION_MINOR(props.apiVersion),
    //      VK_API_VERSION_PATCH(props.apiVersion));

    return score;
}

internal void
CreateInstance(const char *appName)
{
    ArenaTemp scratch = ScratchBegin();

    // Query Vulkan loader API version
    if (volkGetInstanceVersion() <= MINIMUM_VULKAN_API_SUPPORTED)
    {
        REQUIRE("Vulkan loader must support Vulkan %d.%d+.\n",
                  VK_API_VERSION_MAJOR(MINIMUM_VULKAN_API_SUPPORTED),
                  VK_API_VERSION_MINOR(MINIMUM_VULKAN_API_SUPPORTED));
    }

    // Query available instance level extensions
    u32 availableExtCount = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr));
    VkExtensionProperties *availableExtensions = ArenaPushArray(scratch.arena, VkExtensionProperties, availableExtCount);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExtensions));

    // Query required instance extensions
    const char* requiredExtensions[] =
    {
        // TODO: figure out mac and linux
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifdef _WIN32
        "VK_KHR_win32_surface",
#elif __APPLE__
        "VK_KHR_portability_enumeration",
        "VK_EXT_metal_surface",
#elif __linux__
        "VK_KHR_wayland_surface",
#endif
#if USE_VALIDATION
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
    };

    // Check required extensions are supported
    bool allExtensionsFound = true;
    for (const char *required : requiredExtensions)
    {
        bool found = false;
        for (u32 j = 0; j < availableExtCount; j++)
        {
            const char* available = &availableExtensions[j].extensionName[0];
            if (strcmp(required, available) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            allExtensionsFound = false;
            WARN("Could not find required extension: %s", required);
        }
    }
    if (!allExtensionsFound)
    {
        REQUIRE("Could not find all required extensions... exiting");
    }

    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = appName,
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "None",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = MINIMUM_VULKAN_API_SUPPORTED
    };

    VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = ARRAY_SIZE(requiredExtensions),
        .ppEnabledExtensionNames = &requiredExtensions[0],
    };

    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };

#if USE_VALIDATION
    createInfo.enabledLayerCount = ARRAY_SIZE(validationLayers);
    createInfo.ppEnabledLayerNames = &validationLayers[0];
#endif

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &vk->instance));

    // Load instance-level function pointers
    volkLoadInstance(vk->instance);

    // Create debug messenger if requested
#if USE_VALIDATION
    const VkDebugUtilsMessengerCreateInfoEXT debugInfo {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugCallback
    };
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(vk->instance, &debugInfo, nullptr, &vk->debugMessenger));
#endif
    ScratchEnd(scratch);
}

internal void
CreateSurface()
{
    VK_CHECK(glfwCreateWindowSurface(vk->instance, vk->window, nullptr, &vk->surface));
}

internal void
SelectPhysicalDeviceAndQueue()
{
    ArenaTemp scratch = ScratchBegin();

    u32 count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(vk->instance, &count, nullptr));
    VkPhysicalDevice *gpus = ArenaPushArray(scratch.arena, VkPhysicalDevice, count);
    VK_CHECK(vkEnumeratePhysicalDevices(vk->instance, &count, gpus));

    u64 maxScore = 0;
    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    GPUQueue selectedQueue;
    for (u32 i = 0; i < count; i++)
    {
        const VkPhysicalDevice gpu = gpus[i];
        const GPUQueue queue = FindQueue(gpu);
        const u64 score = ScorePhysicalDevice(gpu);
        const bool hasValidQueue = (queue.familyIndex != GPUQueue::INVALID_INDEX);

        if ((score > maxScore) && hasValidQueue)
        {
            selectedDevice = gpu;
            selectedQueue = queue;
            maxScore = score;
        }
    }
    if (selectedDevice == VK_NULL_HANDLE)
    {
        FATAL("No suitable GPUs were found!");
    }

    // Store selected GPU + queues
    vk->physicalDevice = selectedDevice;
    vk->queue = selectedQueue;

    // Store selected GPU properties
    vkGetPhysicalDeviceProperties(selectedDevice, &physicalDeviceProps);
    INFO("GPU selected: %s", physicalDeviceProps.deviceName);
    INFO("Vulkan API version: %d.%d.%d",
             VK_VERSION_MAJOR(physicalDeviceProps.apiVersion),
             VK_VERSION_MINOR(physicalDeviceProps.apiVersion),
             VK_VERSION_PATCH(physicalDeviceProps.apiVersion));

    ScratchEnd(scratch);
}

internal void
CreateDeviceAndQueue()
{
    // TODO: actually validate these are all supported before just requesting them
    VkPhysicalDeviceVulkan11Features features_1_1 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = VK_TRUE
    };

    VkPhysicalDeviceVulkan12Features features_1_2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE
    };

    VkPhysicalDeviceVulkan13Features features_1_3 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
        .maintenance4 = VK_TRUE
    };
    VkPhysicalDeviceVulkan14Features features_1_4 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .maintenance5 = VK_TRUE
    };

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descBufFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
        .descriptorBuffer = VK_TRUE,
    };

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
        .shaderBufferFloat32AtomicAdd = VK_TRUE
    };

    features_1_1.pNext = &features_1_2;
    features_1_2.pNext = &features_1_3;
    features_1_3.pNext = &features_1_4;
    features_1_4.pNext = &descBufFeatures;
    descBufFeatures.pNext = &atomicFeatures;

    VkPhysicalDeviceFeatures2 requiredFeatures { 
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features_1_1,
        .features = { .samplerAnisotropy = VK_TRUE, .shaderInt64 = VK_TRUE }
    };

    // TODO: query extensions
    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
        VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME
    };

    const f32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk->queue.familyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkDeviceCreateInfo deviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &requiredFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = ARRAY_SIZE(deviceExtensions),
        .ppEnabledExtensionNames = &deviceExtensions[0],
    };

    VK_CHECK(vkCreateDevice(vk->physicalDevice, &deviceCreateInfo, nullptr, &vk->device));

    vkGetDeviceQueue(vk->device, vk->queue.familyIndex, 0, &vk->queue.handle); // doesn't fail

    // Load device-level function pointers
    volkLoadDevice(vk->device);
}

internal void
CreateGPUAllocator()
{
    VmaVulkanFunctions vmaVkFunctions {
        vkGetInstanceProcAddr,
        vkGetDeviceProcAddr,
        vkGetPhysicalDeviceProperties,
        vkGetPhysicalDeviceMemoryProperties,
        vkAllocateMemory,
        vkFreeMemory,
        vkMapMemory,
        vkUnmapMemory,
        vkFlushMappedMemoryRanges,
        vkInvalidateMappedMemoryRanges,
        vkBindBufferMemory,
        vkBindImageMemory,
        vkGetBufferMemoryRequirements,
        vkGetImageMemoryRequirements,
        vkCreateBuffer,
        vkDestroyBuffer,
        vkCreateImage,
        vkDestroyImage,
        vkCmdCopyBuffer,
        vkGetBufferMemoryRequirements2,
        vkGetImageMemoryRequirements2,
        vkBindBufferMemory2,
        vkBindImageMemory2,
        vkGetPhysicalDeviceMemoryProperties2,
        vkGetDeviceBufferMemoryRequirements,
        vkGetDeviceImageMemoryRequirements,
    };

    VmaAllocatorCreateFlags flags =
        VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
        VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
        VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT |
        VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
        VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

    VmaAllocatorCreateInfo vmaInfo {
        .flags = flags,
        .physicalDevice = vk->physicalDevice,
        .device = vk->device,
        .pVulkanFunctions = &vmaVkFunctions,
        .instance = vk->instance,
    };

    VK_CHECK(vmaCreateAllocator(&vmaInfo, &vk->allocator));
}

internal void
AllocateCommands()
{
    VkCommandPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = vk->queue.familyIndex,
    };

    VkCommandBufferAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    // create one command pool + command buffer per frame in flight
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateCommandPool(vk->device, &poolInfo, nullptr, &vk->frames[i].commandPool));

        allocInfo.commandPool = vk->frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(vk->device, &allocInfo, &vk->frames[i].commandBuffer));
    }
    // create dedicated transient pool
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VK_CHECK(vkCreateCommandPool(vk->device, &poolInfo, nullptr, &vk->transientPool));
}

internal void 
CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk->frames[i].timelineWaitValue = i;
        VK_CHECK(vkCreateSemaphore(vk->device, &semaphoreInfo, nullptr, &vk->frames[i].imageAcquire));
    }
    for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; i++)
    {
        VkSemaphore* presentReady = &vk->swapchain.presentReady[i];
        VK_CHECK(vkCreateSemaphore(vk->device, &semaphoreInfo, nullptr, presentReady));
    }

    const VkSemaphoreTypeCreateInfo timelineInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = MAX_FRAMES_IN_FLIGHT - 1
    };
    semaphoreInfo.pNext = &timelineInfo;

    VK_CHECK(vkCreateSemaphore(vk->device, &semaphoreInfo, nullptr, &vk->timeline));
}

internal void
CreateDepthBuffers()
{
    u32 w, h;
    w = vk->swapchain.images[0].extent.width;
    h = vk->swapchain.images[0].extent.height;
    for (auto& frame : vk->frames)
        frame.depthBuffer = CreateDepthAttachment(w, h);
}

internal void
DestroyDepthBuffers()
{
    for (auto& frame : vk->frames)
        DestroyImage(&frame.depthBuffer);
}

internal void
SwapchainCreate()
{
    ArenaTemp scratch = ScratchBegin();
    // Query surface capabilities
    const VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = vk->surface
    };
    VkSurfaceCapabilities2KHR capabilities { .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR };
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilities2KHR(vk->physicalDevice, &surfaceInfo, &capabilities));

    // Query present modes
    u32 presentModeCount;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physicalDevice, vk->surface, &presentModeCount, nullptr));

    VkPresentModeKHR *presentModes = ArenaPushArray(scratch.arena, VkPresentModeKHR, presentModeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physicalDevice, vk->surface, &presentModeCount, presentModes));

    // Query formats
    u32 formatCount;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormats2KHR(vk->physicalDevice, &surfaceInfo, &formatCount, nullptr));

    VkSurfaceFormat2KHR *formats = ArenaPushArrayZero(scratch.arena, VkSurfaceFormat2KHR, formatCount);
    for (u32 i = 0; i < formatCount; i++) formats[i].sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormats2KHR(vk->physicalDevice, &surfaceInfo, &formatCount, formats));

    // Figure out extent
    VkExtent2D extent = capabilities.surfaceCapabilities.currentExtent;
    static_assert(sizeof(VkExtent2D) == sizeof(u64));
    if (*((u64*) &extent) == UINT64_MAX) // special value -> query window for extent
    {
        VkExtent2D minExtent = capabilities.surfaceCapabilities.minImageExtent;
        VkExtent2D maxExtent = capabilities.surfaceCapabilities.maxImageExtent;

        int width, height;
        glfwGetWindowSize(vk->window, &width, &height);

        extent.width = CLAMP(minExtent.width, (u32) width, maxExtent.width);
        extent.height = CLAMP(minExtent.height, (u32) height, maxExtent.height);
    }

    // Figure out images to request
    const u32 minImageCount = capabilities.surfaceCapabilities.minImageCount;
    const u32 maxImageCount = capabilities.surfaceCapabilities.maxImageCount;
    const u32 preferredImageCount = MAX_FRAMES_IN_FLIGHT + 1;

    u32 imageCount;
    if (maxImageCount > 0)
        imageCount = CLAMP(minImageCount, preferredImageCount, maxImageCount);
    else // maxImageCount of 0 means no limit
        imageCount = MAX(minImageCount, preferredImageCount);

    // Figure out present mode
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < presentModeCount; i++)
    {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    // Selecting a image format
    VkFormat imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool foundPreferredFormat = false;
    for (u32 i = 0; i < formatCount; i++)
    {
        const bool prefImageFormat = (formats[i].surfaceFormat.format == imageFormat);
        const bool prefColorSpace = (formats[i].surfaceFormat.colorSpace == colorSpace);
        if (prefImageFormat && prefColorSpace)
        {
            foundPreferredFormat = true;
            break;
        }
    }
    if (!foundPreferredFormat) // just pick first one returned if can't find preferred
    {
        imageFormat = formats[0].surfaceFormat.format;
        colorSpace = formats[0].surfaceFormat.colorSpace;
    }

    // Create swapchain
    const VkSwapchainCreateInfoKHR swapchainCreateInfo {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = vk->surface,
        .minImageCount    = imageCount,
        .imageFormat      = imageFormat,
        .imageColorSpace  = colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = capabilities.surfaceCapabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE
    };
    VK_CHECK(vkCreateSwapchainKHR(vk->device, &swapchainCreateInfo, nullptr, &vk->swapchain.handle));

    // According to specs, we could technically get more images than what we asked, so we query image count received
    VK_CHECK(vkGetSwapchainImagesKHR(vk->device, vk->swapchain.handle, &imageCount, nullptr));
    assert(imageCount < MAX_SWAPCHAIN_IMAGES);

    // Save swapchain info
    vk->swapchain.format = imageFormat;
    vk->swapchain.imageCount = imageCount;

    INFO("Swapchain: received %u images of size %ux%u.", imageCount, extent.width, extent.height);

    // Retrieve images
    VkImage swapImages[MAX_SWAPCHAIN_IMAGES]; 
    VK_CHECK(vkGetSwapchainImagesKHR(vk->device, vk->swapchain.handle, &imageCount, swapImages));

    // Create image views
    VkImageViewCreateInfo imageViewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,

        .format = imageFormat,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1 }
    };

    for (u32 i = 0; i < imageCount; i++)
    {
        Image& image = vk->swapchain.images[i];

        // Create image view
        imageViewInfo.image = swapImages[i];
        VK_CHECK(vkCreateImageView(vk->device, &imageViewInfo, nullptr, &image.imageView));

        // fill out image struct
        // image.type = IMAGE_TYPE_SWAPCHAIN;
        image.handle = swapImages[i];
        image.extent = {extent.width, extent.height, 1};
        image.format = imageFormat;
        image.lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    CreateDepthBuffers();

    ScratchEnd(scratch);
}

internal void
SwapchainDestroy()
{
    const u32 imageCount = vk->swapchain.imageCount;
    for (u32 i = 0; i < imageCount; i++)
    {
        vkDestroyImageView(vk->device, vk->swapchain.images[i].imageView, nullptr);
        // vkDestroySemaphore(vk->device, vk->swapchain.imageAcquireSemaphores[i], nullptr);
    }
    vkDestroySwapchainKHR(vk->device, vk->swapchain.handle, nullptr);
}

internal void
SwapchainResize()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(vk->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(vk->window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vk->device);
    SwapchainDestroy();
    SwapchainCreate();
    DestroyDepthBuffers();
    CreateDepthBuffers();

    vk->swapchain.resizeRequested = false;
}

void
VulkanInit(GLFWwindow* window, const char *title)
{
    VK_CHECK(volkInitialize());

    global VulkanContext vkctx;
    vkctx.frameArena = ArenaAlloc(KB(2));
    vkctx.window = window;
    vkctx.swapchain.resizeRequested = false;
    vkctx.currentFrame = 0;
    vk = &vkctx;
    CreateInstance(title);
    CreateSurface();
    SelectPhysicalDeviceAndQueue();
    CreateDeviceAndQueue();
    CreateGPUAllocator();
    AllocateCommands();
    SwapchainCreate();
    CreateSyncObjects();
    BindlessInit();
}

FrameData*
VulkanStartFrame()
{
    // if needed, rebuild swapchain
    if (vk->swapchain.resizeRequested) SwapchainResize();

    PerFrameInfo& frameInfo = vk->frames[vk->currentFrame];
    const VkSemaphoreWaitInfo waitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &vk->timeline,
        .pValues = &frameInfo.timelineWaitValue
    };
    VK_CHECK(vkWaitSemaphores(vk->device, &waitInfo, 200'000'000));

    // Acquire image
    VkResult result = vkAcquireNextImageKHR(vk->device,
                                            vk->swapchain.handle,
                                            200'000'000,
                                            frameInfo.imageAcquire,
                                            VK_NULL_HANDLE,
                                            &vk->swapchain.currentImage);

    FrameData* frame = nullptr;

    if ((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR)) [[likely]]
    {
        const VkCommandBuffer cmd = frameInfo.commandBuffer;

        frame = ArenaPushArray(vk->frameArena, FrameData, 1);
        frame->index = vk->currentFrame;
        frame->swapchainImage = &vk->swapchain.images[vk->swapchain.currentImage];
        frame->commandBuffer = cmd;
        frame->swapchainImage->lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        frame->swapchainImage->lastStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        frame->depthBuffer = &vk->frames[vk->currentFrame].depthBuffer;

        // Reset command buffer before giving to user
        VK_CHECK(vkResetCommandPool(vk->device, frameInfo.commandPool, 0));
        const VkCommandBufferBeginInfo cmdBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        // Set default dynamic state
        // --- Viewport & Scissor ---
        const VkExtent3D extent = frame->swapchainImage->extent;
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = extent.height;
        viewport.width  = (float) extent.width;
        viewport.height = -((float) extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewportWithCount(cmd, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {extent.width, extent.height};
        vkCmdSetScissorWithCount(cmd, 1, &scissor);

        // --- Depth & Stencil ---
        vkCmdSetDepthTestEnable(cmd, VK_TRUE);
        vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
        vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
        vkCmdSetStencilTestEnable(cmd, VK_FALSE);

        // --- Rasterization ---
        vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
        vkCmdSetLineWidth(cmd, 1.0f);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
        vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);

        // --- Blending ---
        float blendConstants[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        vkCmdSetBlendConstants(cmd, blendConstants);

        CmdBindBindlessDescriptors(cmd);
    }
    else if (result == VK_TIMEOUT) {
        uint64_t counter = 0;
        vkGetSemaphoreCounterValue(vk->device, vk->timeline, &counter);
        printf("WAIT TIMEOUT: slot=%u want=%llu counter=%llu\n",
               vk->currentFrame, frameInfo.timelineWaitValue, counter);
    }
    else
    {
        VK_CHECK(result);
    }
    return frame;
}

void
VulkanEndFrame(FrameData* frame)
{
    // Transition to present
    ImageDependency toPresent = {
        .image = frame->swapchainImage,
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access = VK_ACCESS_2_NONE,
        .stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
    };

    CmdPipelineBarrier(frame->commandBuffer, {
        .imageDependencyCount = 1,
        .pImageDependencies = &toPresent,
    });

    VK_CHECK(vkEndCommandBuffer(frame->commandBuffer));

    PerFrameInfo& frameInfo = vk->frames[vk->currentFrame];
    const u64 timelineSignal = frameInfo.timelineWaitValue + MAX_FRAMES_IN_FLIGHT;
    const VkSemaphore acquireSemaphore = frameInfo.imageAcquire;
    const VkSemaphore submitSemaphore = vk->swapchain.presentReady[vk->swapchain.currentImage];

    // When we submit, we must first wait on acquiring the swapchain image
    const VkSemaphoreSubmitInfo waitSemaphores[] = {
        // Image acquisition semaphore
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = acquireSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        }
    };

    // When we are done rendering, we need to...
    // - signal the frame that is MAX_FRAMES_IN_FLIGHT in the future
    // - signal that our queue submission is done (and ready for presentation)
    const VkSemaphoreSubmitInfo signalSemaphores[] = {
        // timeline semaphore
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = vk->timeline,
            .value = timelineSignal,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        },
        // submit semaphore
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = submitSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        },
    };

    const VkCommandBufferSubmitInfo cmdInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = frame->commandBuffer
    };

    const VkSubmitInfo2 submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .flags = 0,
        .waitSemaphoreInfoCount = ARRAY_SIZE(waitSemaphores),
        .pWaitSemaphoreInfos = &waitSemaphores[0],
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = ARRAY_SIZE(signalSemaphores),
        .pSignalSemaphoreInfos = &signalSemaphores[0]
    };

    VK_CHECK(vkQueueSubmit2(vk->queue.handle, 1, &submitInfo, VK_NULL_HANDLE));

    const VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &submitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &vk->swapchain.handle,
        .pImageIndices = &vk->swapchain.currentImage,
        .pResults = nullptr
    };

    VkResult res = vkQueuePresentKHR(vk->queue.handle, &presentInfo);
    if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR))
    {
        VulkanRequestResize();
    }
    else if (res != VK_SUCCESS)
    {
        WARN("Failed to present to swapchain");
        assert(false);
    }

    vk->currentFrame = (vk->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    frameInfo.timelineWaitValue = timelineSignal;
    ArenaClear(vk->frameArena);
}

VkInstance VulkanGetInstance() { return vk->instance; }
VkDevice   VulkanGetDevice() { return vk->device; }
VkPhysicalDevice VulkanGetPhysicalDevice() { return vk->physicalDevice; }
VkCommandPool    VulkanGetTransientPool() { return vk->transientPool; }
VkQueue          VulkanGetQueue() { return vk->queue.handle; }
u32              VulkanGetQueueFamilyIndex() { return vk->queue.familyIndex; }
VmaAllocator     VulkanGetAllocator() { return vk->allocator; }
VkFormat         VulkanGetSwapchainFormat() { return vk->swapchain.format; }
u32              VulkanGetSwapchainImageCount() { return vk->swapchain.imageCount; }
GLFWwindow*      VulkanGetWindow() { return vk->window; }
void             VulkanRequestResize() { vk->swapchain.resizeRequested = true; }

VkExtent2D VulkanGetSwapchainExtent()
{
    return {vk->swapchain.images[0].extent.width, vk->swapchain.images[0].extent.height };
}
const VkPhysicalDeviceProperties* VulkanGetPhysicalDeviceProperties() { return &physicalDeviceProps; }
