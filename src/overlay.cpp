#include "overlay.h"

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_vulkan.h"
#include "third_party/imgui/imgui_impl_glfw.h"

#include "core/utils.h"
#include "core/arena.h"
#include "vulkan/utils.h"
#include "vulkan/context.h"

#include <stdio.h>

enum class WidgetType : u32
{
    Combo,
    Checkbox,
    SliderFloat,
    SliderFloat3
};

struct ComboData
{
    int *pCurr;
    int itemCount;
    const char *const *items;
};

struct CheckboxData
{
    bool *pBool;
};

struct SliderFloatData
{
    f32 *pFloat;
    f32 min;
    f32 max;
};

struct SliderFloat3Data // technically not necessary since SliderFloatData has same memory layout, but for clarity
{
    f32 *pFloat3; // pointer to consecutive memory of 3 floats
    f32 min;
    f32 max;
};

struct WidgetData
{
    WidgetType type;
    const char *pName;
    bool isVisible;
    union
    {
        ComboData           combo;
        CheckboxData        checkbox;
        SliderFloatData     sliderFloat;
        SliderFloat3Data    sliderFloat3;
    };
};

internal Arena *arena;
internal constexpr u32 MAX_WIDGET_ARRAY_LENGTH = 16;
internal WidgetData widgetArr[MAX_WIDGET_ARRAY_LENGTH] = {};
internal u32 widgetCount = 0;

internal void
OverlayBeginRendering()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

internal void
OverlayEndRendering(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd); 
}

internal void
OverlayRenderDeviceStats()
{
    const VkPhysicalDeviceProperties* physDevProps = VulkanGetPhysicalDeviceProperties();

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    char deviceStatsText[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE + 256];
    snprintf(deviceStatsText, sizeof(deviceStatsText),
            "Vulkan API Version: %d.%d.%d\nDevice Name: %s",
            VK_API_VERSION_MAJOR(physDevProps->apiVersion),
            VK_API_VERSION_MINOR(physDevProps->apiVersion),
            VK_API_VERSION_PATCH(physDevProps->apiVersion),
            physDevProps->deviceName);

    ImVec2 textSize = ImGui::CalcTextSize(deviceStatsText);
    ImVec2 windowPos = ImVec2(10.0f, displaySize.y - textSize.y - 20.0f);
    ImGui::SetNextWindowPos(windowPos);

    ImGui::Begin("Device Statistics", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Text("%s", deviceStatsText);

    ImGui::End();
}

internal void
OverlayRenderStats(const FrameStats& stats)
{
    char statsText[256];
    f32 fps = ImGui::GetIO().Framerate;
    snprintf(statsText, sizeof(statsText),
             "Frame time: %.1f ms\nSimulation steps: %u\nParticle count: %u",
             1000.0f / fps, stats.simSteps, stats.particleCount);

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 textSize = ImGui::CalcTextSize(statsText);
    ImVec2 windowPos = ImVec2(displaySize.x - textSize.x - 20.0f, 10.0f);
    ImGui::SetNextWindowPos(windowPos);
    ImGui::Begin("Frame stats", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Text("%s", statsText);

    ImGui::End();
}

internal void
OverlayAddWidget(const WidgetData& w)
{
    switch (w.type)
    {
        case WidgetType::Combo:
        {
            ImGui::Combo(w.pName, w.combo.pCurr, w.combo.items, w.combo.itemCount);
        } break;
        case WidgetType::Checkbox:
        {
            ImGui::Checkbox(w.pName, w.checkbox.pBool);
        } break;
        case WidgetType::SliderFloat:
        {
            ImGui::SliderFloat(w.pName, w.sliderFloat.pFloat, w.sliderFloat.min, w.sliderFloat.max);
        } break;
        case WidgetType::SliderFloat3:
        {
            ImGui::SliderFloat3(w.pName, w.sliderFloat3.pFloat3, w.sliderFloat3.min, w.sliderFloat3.max);
        } break;
        default:
        {
            assert(false);
        } break;
    }
}

internal void
OverlayRenderSettingsPane()
{
    ImVec2 pos = ImGui::GetMainViewport()->WorkPos;
    ImGui::SetNextWindowPos(ImVec2(pos.x + 10.0f, pos.y + 10.0f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::BeginChild("Options", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY))
        {
            for (const WidgetData& w : widgetArr)
            {
                if (w.isVisible) OverlayAddWidget(w);
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}


void
OverlayAddCombo(const char* pName, int* pCurr, std::initializer_list<const char *> items)
{
    if (widgetCount < MAX_WIDGET_ARRAY_LENGTH)
    {
        // allocate strings in contiguous memory for user, string literals themselves are in static, but they need to
        // be contiguous, so will have our own contiguous array of pointers
        const char **strArr = ArenaPushArrayZero(arena, const char *, items.size());

        u32 i = 0;
        for (const char * name : items)
        {
            strArr[i++] = name;
        }
        WidgetData& w = widgetArr[widgetCount++];
        w.type = WidgetType::Combo;
        w.pName = pName;
        w.isVisible = true;
        w.combo = {
            .pCurr = pCurr,
            .itemCount = (int) items.size(),
            .items = strArr
        };
    }
    else
    {
        assert(false);
    }
}

void
OverlayAddCheckbox(const char* pName, bool* pBool)
{
    if (widgetCount < MAX_WIDGET_ARRAY_LENGTH)
    {
        WidgetData& w = widgetArr[widgetCount++];
        w.type = WidgetType::Checkbox;
        w.pName = pName;
        w.isVisible = true;
        w.checkbox = {
            .pBool = pBool,
        };
    }
    else
    {
        assert(false);
    }
}

void OverlayAddSliderFloat(const char* pName, f32* pFloat, f32 min, f32 max)
{
    if (widgetCount < MAX_WIDGET_ARRAY_LENGTH)
    {
        WidgetData& w = widgetArr[widgetCount++];
        w.type = WidgetType::SliderFloat;
        w.pName = pName;
        w.isVisible = true;
        w.sliderFloat = {
            .pFloat = pFloat,
            .min = min,
            .max = max
        };
    }
    else
    {
        assert(false);
    }
}

void OverlayAddSliderFloat3(const char* pName, f32* pFloat3, f32 min, f32 max)
{
    if (widgetCount < MAX_WIDGET_ARRAY_LENGTH)
    {
        WidgetData& w = widgetArr[widgetCount++];
        w.type = WidgetType::SliderFloat3;
        w.pName = pName;
        w.isVisible = true;
        w.sliderFloat3 = {
            .pFloat3 = pFloat3,
            .min = min,
            .max = max
        };
    }
    else
    {
        assert(false);
    }
}

void
OverlayInit()
{
    const VkDevice device = VulkanGetDevice();

    // TODO: tune these pool sizes
    // TODO: do these need to use my descriptor buffer since I use it for my renderer?
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    };
    VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1000,
        .poolSizeCount = ARRAY_SIZE(poolSizes),
        .pPoolSizes = &poolSizes[0]
    };

    VkDescriptorPool descriptorPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = nullptr;
    ImGui_ImplGlfw_InitForVulkan(VulkanGetWindow(), true);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = VulkanGetInstance();
    initInfo.PhysicalDevice = VulkanGetPhysicalDevice();
    initInfo.Device = VulkanGetDevice();
    initInfo.QueueFamily = VulkanGetQueueFamilyIndex();
    initInfo.Queue = VulkanGetQueue();
    initInfo.DescriptorPool = descriptorPool;
    initInfo.MinImageCount = VulkanGetSwapchainImageCount();
    initInfo.ImageCount = VulkanGetSwapchainImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;

    VkFormat format = VulkanGetSwapchainFormat();
    initInfo.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
    };

    ImGui_ImplVulkan_Init(&initInfo);

    arena = ArenaAlloc(KB(1));
}

void
OverlayRender(VkCommandBuffer cmd, const FrameStats& stats)
{
    OverlayBeginRendering();
    OverlayRenderSettingsPane();

    // TODO: change API to allow caller to specify overlay text (and corner of screen to write to)
    // Then, we don't need this awkward FrameStats struct
    OverlayRenderDeviceStats();
    OverlayRenderStats(stats);
    OverlayEndRendering(cmd);
}



