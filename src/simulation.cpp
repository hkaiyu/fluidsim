#include "simulation.h"

#include "vulkan/commands.h"
#include "vulkan/context.h"
#include "vulkan/generated/shaders.h"
#include "vulkan/immediate_commands.h"

#include "overlay.h"

#include <glm/glm.hpp>
#include <volk/volk.h>

struct PingPongImage
{
    u32 curr = 0;
    Image images[2];

    Image *read() { return &images[curr]; };
    Image *write() { return &images[curr ^ 1]; };
    void swap() { curr ^= 1; };
};

internal constexpr s32 PAD = 2;
internal constexpr f32 SIM_DT = 0.015f;
internal constexpr u32 MAX_STEPS_PER_FRAME = 3;

internal u32 nparticles;
internal SimParams params;
internal bool simulationPaused = false;
internal bool showBounds = false;
internal RenderMethod renderMethod;
internal GravityMode gravityMode;

// TODO: move to per frame uniform
internal f32 rangeSigma = 10.0f;
internal f32 spatialSigma = 5.0f;
internal f32 blurKernelRadius = 7.0f;

// gpu resources
internal Buffer particleBuffer;
internal Buffer gridBuffer;
internal Buffer paramBuffer;
internal Sampler linearSamp;
internal Image thicknessImage;
internal PingPongImage depthImage;
internal Image depthBuffer; // a literal depth buffer, different from the world-distance depth we compute
internal Image renderImage;

// compute pipelines
internal Pipeline clearGrid;
internal Pipeline scatterMomentum; // P2G
internal Pipeline scatterForce;    // P2G
internal Pipeline updateGrid;      // P2G
internal Pipeline updateParticles; // G2P
internal Pipeline hFilter;
internal Pipeline vFilter;

// render pipelines
internal Pipeline renderDepth;
internal Pipeline renderThickness;
internal Pipeline renderFluid;
internal Pipeline renderBillboards;
internal Pipeline boundingBoxView;

internal PingPongImage
CreatePingPongImage(const ImageCreateInfo &info)
{
    return PingPongImage{.curr = 0, .images = {CreateImage(info), CreateImage(info)}};
}

internal VkExtent3D
GetSimulationRenderExtent()
{
    const VkExtent2D swapchainExtent = VulkanGetSwapchainExtent();
    return {swapchainExtent.width, swapchainExtent.height, 1};
}

internal void
CreateSimulationRenderTargets(VkExtent3D extent)
{
    depthImage = CreatePingPongImage({
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = extent,
        .format = VK_FORMAT_R32_SFLOAT,
        .isAttachment = true,
        .isStorage = true,
        .isSampled = true
    });

    thicknessImage = CreateImage({
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = extent,
        .format = VK_FORMAT_R32_SFLOAT,
        .isAttachment = true,
        .isStorage = true,
        .isSampled = true
    });

    renderImage = CreateImage({
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = extent,
        .format = VulkanGetSwapchainFormat(), // keep as swapchain format
        .isAttachment = true,
    });

    depthBuffer = CreateDepthAttachment(extent.width, extent.height);
}

internal bool
NeedsSimulationRenderTargetResize(VkExtent3D desiredExtent)
{
    const VkExtent3D currExtent = depthImage.images[0].extent;
    if ((currExtent.width != desiredExtent.width) || (currExtent.height != desiredExtent.height))
        return true;

    if (renderImage.format != VulkanGetSwapchainFormat())
        return true;

    return false;
}

internal void
EnsureSimulationRenderTargets()
{
    const VkExtent3D desiredExtent = GetSimulationRenderExtent();
    if (!NeedsSimulationRenderTargetResize(desiredExtent))
        return;

    DestroyImage(&depthImage.images[0]);
    DestroyImage(&depthImage.images[1]);
    DestroyImage(&thicknessImage);
    DestroyImage(&depthBuffer);
    DestroyImage(&renderImage);

    CreateSimulationRenderTargets(desiredExtent);
}

internal void
Compute2ComputeBarrier(VkCommandBuffer cmd)
{
    MemoryDependency barrier = {
        .srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
    };
    CmdPipelineBarrier(cmd, {.memoryDependencyCount = 1, .pMemoryDependencies = &barrier});
}

internal void
Compute2GraphicsBarrier(VkCommandBuffer cmd)
{
    MemoryDependency barrier = {
        .srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStage = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        .dstAccess = VK_ACCESS_2_MEMORY_READ_BIT,
    };
    CmdPipelineBarrier(cmd, {.memoryDependencyCount = 1, .pMemoryDependencies = &barrier});
}

internal void
SetViewportScissorToExtent(VkCommandBuffer cmd, VkExtent3D extent)
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = (float) extent.height;
    viewport.width = (float) extent.width;
    viewport.height = -((float) extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewportWithCount(cmd, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {extent.width, extent.height};
    vkCmdSetScissorWithCount(cmd, 1, &scissor);
}

internal void
InitializeParticles(glm::uvec3 bounds, Buffer *buffer)
{
    struct Particle
    {
        glm::vec4 C0;
        glm::vec4 C1;
        glm::vec4 C2;
        glm::vec3 pos;
        f32 mass;
        glm::vec3 vel;
        f32 _unused;
    };
    static_assert(sizeof(Particle) % 16 == 0);

    f32 height = 40.0f;
    f32 width = 40.0f;
    f32 depth = 40.0f;
    glm::vec3 center = glm::vec3(bounds) * 0.5f;
    glm::vec3 offset = glm::vec3(width, height, depth) * 0.5f;
    glm::uvec3 spawnAA = glm::uvec3(center - offset);
    glm::uvec3 spawnBB = glm::uvec3(center + offset);
    assert(spawnAA < spawnBB && "Invalid spawn box");
    spawnBB = glm::min(spawnBB, bounds);

    float spacing = 0.75f;
    glm::uvec3 counts = glm::uvec3(glm::vec3(spawnBB - spawnAA) / spacing);
    nparticles = counts.x * counts.y * counts.z;

    INFO("Simulation running in local coordinates: [%u, %u, %u], [%u, %u, %u].",
       0, 0, 0, bounds.x, bounds.y, bounds.z);

    INFO("Spawning %u particles in box: [%u, %u, %u], [%u, %u, %u].", nparticles,
       spawnAA.x, spawnAA.y, spawnAA.z, spawnBB.x, spawnBB.y, spawnBB.z);

    // create buffer
    size_t size = nparticles * sizeof(Particle);
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    *buffer = CreateBuffer(size, flags, false);

    // create temporary init pipeline
    Pipeline initParticles =
      CreateComputePipeline({shaders::mls_mpm_InitParticles_comp});

    // dispatch compute to do initial values
    VkCommandBuffer cmd = BeginImmediateCommands();
    CmdBindComputePipeline(cmd, initParticles);
    struct {
        glm::ivec3 boundAA;
        u32 nparticles;
        glm::ivec3 boundBB;
        f32 spacing;
        u64 particlesAddr;
    } pc = {
        spawnAA, nparticles, spawnBB, spacing, buffer->gpuAddress,
    };

    CmdPushConstants(cmd, 0, sizeof(pc), &pc);
    CmdDispatch(cmd, (nparticles + 511) / 512, 1, 1);
    SubmitImmediateCommands(cmd);

    DestroyPipeline(initParticles);
}

internal void 
InitializeGrid(glm::uvec3 bounds, Buffer *grid)
{
    struct Cell
    {
        glm::vec3 pv; // momentum during P2G -> becomes velocity
        f32 mass;
    };
    static_assert(sizeof(Cell) == 16);

    // create buffer
    glm::ivec3 paddedExtent = bounds + glm::uvec3(2 * PAD); // pad on both sides each axis
    size_t ncells = (size_t)paddedExtent.x * paddedExtent.y * paddedExtent.z;
    size_t size = ncells * sizeof(Cell);
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    *grid = CreateBuffer(size, flags, false);
}

internal void
InitializeParams(glm::uvec3 bounds, Buffer *paramBuffer, SimParams *params)
{
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    *paramBuffer = CreateBuffer(sizeof(SimParams), flags, true);

    glm::ivec3 paddedExtent = bounds + glm::uvec3(2 * PAD); // pad on both sides each axis
    size_t ncells = (size_t)paddedExtent.x * paddedExtent.y * paddedExtent.z;

    params->bounds = bounds;
    params->gridPad = PAD;
    params->invBounds = 1.0f / glm::vec3(bounds);
    params->ncells = ncells;

    params->gravity = DEFAULT_GRAVITY_STRENGTH;
    params->restDensity = 1.0f;
    params->dynamicViscosity = DEFAULT_VISCOSITY;
    params->eosStiffness = DEFAULT_EOS_STIFFNESS;
    params->eosPower = DEFAULT_EOS_POWER;

    params->extinctionCoeff = DEFAULT_EXTINCTION_COEFF;
    params->densityThreshold = DEFAULT_DENSITY_THRESHOLD;
    params->sunDir = glm::normalize(DEFAULT_SUN_DIR);
    params->densityMultiplier = DEFAULT_DENSITY_MULT;
    params->shadowMultiplier = DEFAULT_SHADOW_MULT;
    params->shadowThreshold = 4.0f;
    params->stepSize = DEFAULT_STEP_SIZE;
    params->refractMultiplier = 2.0;
}

void
SimulationInit(glm::uvec3 bounds)
{
    // Setup compute pipelines
    clearGrid = CreateComputePipeline({shaders::mls_mpm_ClearGrid_comp});
    scatterMomentum = CreateComputePipeline({shaders::mls_mpm_ScatterMomentum_comp});
    scatterForce = CreateComputePipeline({shaders::mls_mpm_ScatterForce_comp});
    updateGrid = CreateComputePipeline({shaders::mls_mpm_UpdateGrid_comp});
    updateParticles = CreateComputePipeline({shaders::mls_mpm_UpdateParticles_comp});
    hFilter = CreateComputePipeline({ shaders::smoothing_BilateralFilter_H_comp });
    vFilter = CreateComputePipeline({ shaders::smoothing_BilateralFilter_V_comp });

    // Setup graphics pipelines
    VkPipelineColorBlendAttachmentState attachments[] = {
        {  // additive blend
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT
        }
    };
    renderDepth = CreateGraphicsPipeline({
        .vert = shaders::render_particles_vsMain_vert,
        .frag = shaders::render_particles_fsMain_frag,
    });

    renderThickness = CreateGraphicsPipeline({
        .vert = shaders::render_thickness_VSMain_vert,
        .frag = shaders::render_thickness_FSMain_frag,
    }, attachments, ARRAY_SIZE(attachments));

    renderFluid = CreateGraphicsPipeline({
        .vert = shaders::full_screen_FullScreenTriangle_vert,
        .frag = shaders::render_fluid_RenderFluid_frag
    });

    renderBillboards = CreateGraphicsPipeline({
        .vert = shaders::render_billboards_VSMain_vert,
        .frag = shaders::render_billboards_FSMain_frag
    });

    boundingBoxView = CreateGraphicsPipeline({
        .vert = shaders::bounding_box_vsMain_vert,
        .frag = shaders::bounding_box_fsMain_frag,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST
    });

    CreateSimulationRenderTargets(GetSimulationRenderExtent());
    linearSamp = CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

    InitializeParams(bounds, &paramBuffer, &params);
    InitializeParticles(bounds, &particleBuffer);
    InitializeGrid(bounds, &gridBuffer);

    // Add tweakables to UI
    OverlayAddCombo("Render method", (int *)&renderMethod, {"Screen space", "Billboard"});
    OverlayAddCombo("Gravity mode", (int *)&gravityMode, {"Normal", "Swirl", "Center"});
    OverlayAddCheckbox("Show simulation bounds", &showBounds);
    OverlayAddSliderFloat("EOS stiffness", &params.eosStiffness, 10.0f, 30.0f);
    OverlayAddSliderFloat("EOS power", &params.eosPower, 1.0f, 4.0f);
    OverlayAddSliderFloat("Viscosity", &params.dynamicViscosity, 0.05f, 2.0f);
    OverlayAddSliderFloat("Range sigma", &rangeSigma, 1.0f, 10.0f);
    OverlayAddSliderFloat("Spatial sigma", &spatialSigma, 5.0f, 15.0f);
    OverlayAddSliderFloat("Kernel radius", &blurKernelRadius, 3.0f, 15.0f);
    OverlayAddSliderFloat3("Extinction coeff", &params.extinctionCoeff.x, 0.0f, 3.0f);
    OverlayAddSliderFloat("Density multiplier", &params.densityMultiplier, 0.1f, 2.0f);
    OverlayAddSliderFloat("Refract multiplier", &params.refractMultiplier, 1.0f, 10.0f);
}

void
AdvanceSimulation(VkCommandBuffer cmd, f32 frameDt)
{
    f32 dt = std::min(frameDt * 8.0f, 0.1f); // TODO: frame resize causes big jump... for now just clamp dt
    dt = simulationPaused ? 0.0f : dt;

    // update params
    memcpy(paramBuffer.pMapped, &params, sizeof(params));

    // push constants
    struct {
        u64 particlesAddr;
        u64 gridAddr;
        u64 paramsAddr;
        u32 nparticles;
        f32 dt;
        GravityMode gravityMode;
    } pc = {
        particleBuffer.gpuAddress,
        gridBuffer.gpuAddress,
        paramBuffer.gpuAddress,
        nparticles,
        dt,
        gravityMode
    };
    CmdPushConstants(cmd, 0, sizeof(pc), &pc);

    CmdBindComputePipeline(cmd, clearGrid);
    CmdDispatch(cmd, (params.ncells + 511) / 512, 1, 1);
    Compute2ComputeBarrier(cmd);

    CmdBindComputePipeline(cmd, scatterMomentum);
    CmdDispatch(cmd, (nparticles + 511) / 512, 1, 1);
    Compute2ComputeBarrier(cmd);

    CmdBindComputePipeline(cmd, scatterForce);
    CmdDispatch(cmd, (nparticles + 511) / 512, 1, 1);
    Compute2ComputeBarrier(cmd);

    CmdBindComputePipeline(cmd, updateGrid);
    CmdDispatch(cmd, (params.ncells + 511) / 512, 1, 1);
    Compute2ComputeBarrier(cmd);

    CmdBindComputePipeline(cmd, updateParticles);
    CmdDispatch(cmd, (nparticles + 511) / 512, 1, 1);
}

internal void
RenderDepthAndThickness(VkCommandBuffer cmd, VkDeviceAddress cameraData)
{
    struct {
        u64 cameraAddr;
        u64 particlesAddr;
        u64 paramsAddr;
    } pc = {cameraData, particleBuffer.gpuAddress, paramBuffer.gpuAddress};

    CmdPushConstants(cmd, 0, sizeof(pc), &pc);
    MemoryDependency computeDone {
      .srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccess = VK_ACCESS_2_SHADER_WRITE_BIT,
      .dstStage = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
      .dstAccess = VK_ACCESS_2_MEMORY_READ_BIT,
    };

    ImageDependency toRender[] = {
        {
            .image = &thicknessImage,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        },
        {
            .image = depthImage.write(),
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        },
        {
            .image = &depthBuffer,
            .layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        }
    };

    // ensure compute is done and depth image has transitioned layout
    CmdPipelineBarrier(cmd, {
        .memoryDependencyCount = 1,
        .pMemoryDependencies = &computeDone,
        .imageDependencyCount = ARRAY_SIZE(toRender),
        .pImageDependencies = toRender
    });

    SetViewportScissorToExtent(cmd, depthImage.write()->extent);

    CmdBindGraphicsPipeline(cmd, renderDepth);
    CmdBeginRendering(cmd, {
        .colorAttachments = {
            {.image = depthImage.write()},
        },
        .depthAttachment = {
            .image = &depthBuffer,
            .clearValue = {1.0f, 0}
        }
    });
    CmdDraw(cmd, 6 * nparticles);
    CmdEndRendering(cmd);

    CmdBindGraphicsPipeline(cmd, renderThickness);
    CmdBeginRendering(cmd, {
        .colorAttachments = {
            {.image = &thicknessImage }
        },
    });
    CmdDraw(cmd, 6 * nparticles);
    CmdEndRendering(cmd);

    depthImage.swap();
}

internal void
ApplyBilateralFilter(VkCommandBuffer cmd)
{
    VkExtent3D extent = depthImage.images[0].extent;
    struct {
        glm::uvec2 imageRes;
        u32 inTex;
        u32 outTex;
        f32 rangeSigma;
        f32 spatialSigma;
        int kernelRadius;
    } pc = {
        {extent.width, extent.height},
        depthImage.read()->bindlessSlotStorage,
        depthImage.write()->bindlessSlotStorage,
        rangeSigma,
        spatialSigma,
        (int) blurKernelRadius
    };
    CmdPushConstants(cmd, 0, sizeof(pc), &pc);

    // prepare layouts
    ImageDependency startLayout[] = {
        {
            .image = depthImage.write(),
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access = VK_ACCESS_2_SHADER_WRITE_BIT,
            .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        },
        {
            .image = depthImage.read(),
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .access = VK_ACCESS_2_SHADER_READ_BIT,
            .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        }
    };

    CmdPipelineBarrier(cmd, {.imageDependencyCount = ARRAY_SIZE(startLayout), .pImageDependencies = startLayout });

    CmdBindComputePipeline(cmd, vFilter);
    CmdDispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);

    depthImage.swap();
    pc.inTex = depthImage.read()->bindlessSlotStorage;
    pc.outTex = depthImage.write()->bindlessSlotStorage;
    CmdPushConstants(cmd, 0, sizeof(pc), &pc);

    startLayout[0].image = depthImage.write();
    startLayout[1].image = depthImage.read();
    CmdPipelineBarrier(cmd, {.imageDependencyCount = ARRAY_SIZE(startLayout), .pImageDependencies = startLayout });

    CmdBindComputePipeline(cmd, hFilter);
    CmdDispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);
    depthImage.swap();
}

internal void
RenderFluid(VkCommandBuffer cmd, VkDeviceAddress cameraData)
{
    ImageDependency toRender[] = {
        {
            .image = &thicknessImage,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        },
        {
            .image = depthImage.read(),
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        },
        {
            .image = &renderImage,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        }
    };
    CmdPipelineBarrier(cmd, { .imageDependencyCount = ARRAY_SIZE(toRender), .pImageDependencies = toRender });

    VkExtent3D e = renderImage.extent;
    SetViewportScissorToExtent(cmd, e);
    struct {
        u64 cameraAddr;
        u64 paramsAddr;
        glm::uvec2 texResolution;
        u32 sampler;
        u32 depthTex;
        u32 thicknessTex;
    } pc = {
        cameraData,
        paramBuffer.gpuAddress,
        { e.width, e.height },
        linearSamp.bindlessSlot,
        depthImage.read()->bindlessSlotSample,
        thicknessImage.bindlessSlotSample
    };
    CmdPushConstants(cmd, 0, sizeof(pc), &pc);
    CmdBindGraphicsPipeline(cmd, renderFluid);
    CmdBeginRendering(cmd, { .colorAttachments = {{ .image = &renderImage }} });
    CmdDraw(cmd, 3);
    if (showBounds)
    {
        vkCmdSetDepthTestEnable(cmd, VK_FALSE);
        vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
        CmdBindGraphicsPipeline(cmd, boundingBoxView);
        CmdDraw(cmd, 24);
    }
    CmdEndRendering(cmd);
}

internal void
RenderBillboards(VkCommandBuffer cmd, VkDeviceAddress cameraData)
{
    ImageDependency requireDepth = {
        .image  = &depthBuffer,
        .layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .stage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    };
    CmdPipelineBarrier(cmd, { .imageDependencyCount = 1, .pImageDependencies = &requireDepth });
    struct {
        u64 cameraAddr;
        u64 paramsAddr;
        u64 particlesAddr;
    } pc = {
        cameraData,
        paramBuffer.gpuAddress,
        particleBuffer.gpuAddress,
    };
    CmdPushConstants(cmd, 0, sizeof(pc), &pc);
    SetViewportScissorToExtent(cmd, renderImage.extent);
    CmdBindGraphicsPipeline(cmd, renderBillboards);
    CmdBeginRendering(cmd, {
        .colorAttachments = {{ .image = &renderImage }},
        .depthAttachment = {
            .image = &depthBuffer,
            .clearValue = {1.0f, 0}
        }
    });
    CmdDraw(cmd, 6 * nparticles);

    if (showBounds)
    {
        vkCmdSetDepthTestEnable(cmd, VK_FALSE);
        vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
        CmdBindGraphicsPipeline(cmd, boundingBoxView);
        CmdDraw(cmd, 24);
    }
    CmdEndRendering(cmd);

}

Image *
SimulationAdvanceAndRenderImage(VkCommandBuffer cmd, f32 dt, VkDeviceAddress cameraData)
{
    EnsureSimulationRenderTargets();
    AdvanceSimulation(cmd, dt);
    if (renderMethod == RenderMethod::ScreenSpace)
    {
        RenderDepthAndThickness(cmd, cameraData);
        ApplyBilateralFilter(cmd);
        RenderFluid(cmd, cameraData);
    }
    else
    {
        RenderBillboards(cmd, cameraData);
    }
    return &renderImage;
}

u32 SimulationGetParticleCount() { return nparticles; }
void SimulationTogglePause() { simulationPaused = !simulationPaused; }
