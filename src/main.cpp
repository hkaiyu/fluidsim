#include <chrono>
#include <vma/vk_mem_alloc.h>
#include <volk/volk.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Headers
#include "config.h"
#include "core/utils.h"
#include "core/arena.h"
#include "core/threads.h"
#include "vulkan/context.h"
#include "vulkan/utils.h"
#include "vulkan/commands.h"
#include "vulkan/immediate_commands.h"
#include "vulkan/resources.h"
#include "vulkan/pipeline.h"
#include "vulkan/resources.h"
#include "vulkan/generated/shaders.h"
#include "vulkan/bindless.h"
#include "imgui.h"
#include "overlay.h"
#include "camera.hpp"
#include "simulation.h"

// Sources
#include "core/utils.cpp"
#include "core/arena.cpp"
#include "core/threads.cpp"
#include "vulkan/utils.cpp"
#include "vulkan/commands.cpp"
#include "vulkan/immediate_commands.cpp"
#include "vulkan/context.cpp"
#include "vulkan/resources.cpp"
#include "vulkan/pipeline.cpp"
#include "vulkan/bindless.cpp"
#include "overlay.cpp"
#include "camera.cpp"
#include "simulation.cpp"

constexpr internal glm::uvec3 simBounds = { 96, 64, 64 };

struct CameraData
{
    // transforms
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invView;
    glm::mat4 invProj;

    // camera
    glm::vec4 cameraPos; // w = unused
    glm::vec4 cameraDir; // w = unused
};

internal GLFWwindow* window;
internal ArcballCamera cam;

// gpu resources
internal Buffer uniforms[MAX_FRAMES_IN_FLIGHT];

internal void
SetupCamera(ArcballCamera* camera)
{
    // camera settings
    f32 aspect = (float) WINDOW_WIDTH / WINDOW_HEIGHT;
    f32 nearz = 0.1f;
    f32 farz = 1000.0f;

    camera->setPerspectiveProjection(glm::radians(60.0f), aspect, nearz, farz);

    // camera positioning
    glm::vec3 extent = glm::vec3(simBounds);
    glm::vec3 lookAt = extent * 0.5f;
    f32 max = MAX(extent.x, extent.y); max = MAX(max, extent.z);
    glm::vec3 cameraPos = lookAt + glm::vec3(0.0f, 0.0f, -max * 1.25f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    camera->setCameraView(cameraPos, lookAt, up);
}

internal void
SetupWindow(const char* title, u32 width, u32 height)
{
    if (!glfwInit())
    {
        FATAL("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        FATAL("Failed to create GLFW window");
    }

    // TODO: rework callback system
    // TODO: fix window resizing
    glfwSetWindowSizeCallback(window, [](GLFWwindow*, int w, int h) {
        // TODO: other systems may need to destroy/create GPU resources on swapchain resize, but this can only be done
        // safely from the Vulkan code, so other systems need to be able to register callbacks with Vulkan subsystem.
        // So any GPU resources that are used in other systems need to centralize around Vulkan system, not this code
        VulkanRequestResize();
        // cam.updateAspect((float) w / h);
    });

    // TODO: handle settings through UI over this
    glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS)
        {
            if (key == GLFW_KEY_SPACE)
            {
                SimulationTogglePause();
            }
        }
    });

    glfwSetScrollCallback(window, [](GLFWwindow*, double xoff, double yoff) {
        cam.processGLFWScrollEvent(xoff, yoff);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow*, double xpos, double ypos) {
        // VkExtent2D extent = VulkanGetSwapchainExtent();
        cam.processGLFWCursorMoveEvent(xpos, ypos, (float) WINDOW_WIDTH, (float) WINDOW_HEIGHT);
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods) {
        if (!ImGui::GetIO().WantCaptureMouse) // ignore mouse moving camera if mouse on ImGui window
        {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            cam.processGLFWMouseButtonEvent(button, action, mods, xpos, ypos);
        }
    });
}

int
main(int argc, char* argv[])
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using DurationSecs = std::chrono::duration<f32>;

    // ===== Initialization =====
    const char *title = "MLS-MPM Fluid Simulation";
    SetupWindow(title, WINDOW_WIDTH, WINDOW_HEIGHT);
    SetupCamera(&cam);
    VulkanInit(window, title);
    OverlayInit();
    SimulationInit(simBounds);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniforms[i] = CreateBuffer(sizeof(CameraData),
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         true);
    }

    // ===== Main Loop =====
    f32 dt = 0.0f;
    TimePoint lastTime = Clock::now();
    while (!glfwWindowShouldClose(window))
    // for (int i = 0; i < 10; i++)
    {
        TimePoint now = Clock::now();
        dt = std::chrono::duration_cast<DurationSecs>(now - lastTime).count();
        lastTime = now;

        glfwPollEvents();

        FrameData* frame = VulkanStartFrame();
        if (frame) [[likely]]
        {
            const u32 ringIdx = frame->index;
            const VkCommandBuffer cmd = frame->commandBuffer;
            Image* swapchainImage = frame->swapchainImage;

            CameraData cd;
            cd.view = cam.getViewMatrix();
            cd.proj = cam.getProjMatrix();
            cd.invView = glm::inverse(cd.view);
            cd.invProj = glm::inverse(cd.proj);
            cd.cameraPos = glm::vec4(cam.getCameraPos(), 1.0f);
            cd.cameraDir = glm::vec4(cam.getViewDir(), 1.0f);

            // Write to uniform buffer
            const auto& ubo = uniforms[ringIdx];
            memcpy(ubo.pMapped, &cd, sizeof(cd));

            Image *render = SimulationAdvanceAndRenderImage(cmd, dt, ubo.gpuAddress);

            ImageDependency blits[] = {
                {
                    .image = render,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .access = VK_ACCESS_2_TRANSFER_READ_BIT,
                    .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                },
                {
                    .image = swapchainImage,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                }
            };
            CmdPipelineBarrier(cmd, { .imageDependencyCount = ARRAY_SIZE(blits), .pImageDependencies = blits });
            CmdBlitImage(cmd, render, swapchainImage, VK_FILTER_LINEAR);

            // render overlay directly to swapchain
            ImageDependency renderOverlay[] = {
                {
                    .image = swapchainImage,
                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                },
            };
            CmdPipelineBarrier(cmd, {
                .imageDependencyCount = ARRAY_SIZE(renderOverlay),
                .pImageDependencies = renderOverlay,
            });
            CmdBeginRendering(cmd, {
                .colorAttachments = {{ .image = swapchainImage, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD }},
            });
            OverlayRender(cmd, { 1, SimulationGetParticleCount() });
            CmdEndRendering(cmd);

            VulkanEndFrame(frame);
        }
    }
    return 0;
}
