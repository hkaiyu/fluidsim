#pragma once

#include "resources.h"

#include <volk/volk.h>

struct GLFWwindow;

struct FrameData
{
    u32 index;
    Image* swapchainImage;
    Image* depthBuffer;
    VkCommandBuffer commandBuffer;
};

void            VulkanInit(GLFWwindow *window, const char *title);
FrameData*      VulkanStartFrame();
void            VulkanEndFrame(FrameData* frame);
VkExtent2D      VulkanSwapchainExtent();

// Ideally, we would have the renderer own resources in pools and returns handles to users
// But for the needs of this application and for sake of time, we do this
// TODO: implement this later, render to fixed size for now
void VulkanRegisterSwapchainRelativeImage(Image* image, float swapchainRelativeScale);

// TODO: maybe just have a large context object passed around everywhere at this point...
// Many of these want to be globally accessed but maybe there is better way to seperate these
VkInstance       VulkanGetInstance();
VkDevice         VulkanGetDevice();
VkPhysicalDevice VulkanGetPhysicalDevice();
VkCommandPool    VulkanGetTransientPool();
VkQueue          VulkanGetQueue();
u32              VulkanGetQueueFamilyIndex();
VmaAllocator     VulkanGetAllocator();
VkExtent2D       VulkanGetSwapchainExtent();
VkFormat         VulkanGetSwapchainFormat();
u32              VulkanGetSwapchainImageCount();
GLFWwindow*      VulkanGetWindow();
void             VulkanRequestResize();


const VkPhysicalDeviceProperties* VulkanGetPhysicalDeviceProperties();
