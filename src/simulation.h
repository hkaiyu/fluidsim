#pragma once

#include "core/utils.h"
#include "vulkan/resources.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

inline constexpr f32        DEFAULT_GRAVITY_STRENGTH    = 0.75f;
inline constexpr f32        DEFAULT_EOS_STIFFNESS       = 18.0f;
inline constexpr f32        DEFAULT_EOS_POWER           = 1.2f;

inline constexpr f32        DEFAULT_VISCOSITY           = 0.1f;
inline constexpr f32        DEFAULT_DENSITY_MULT        = 0.1f;
inline constexpr f32        DEFAULT_SHADOW_MULT         = 0.1f;
inline constexpr f32        DEFAULT_DENSITY_THRESHOLD   = 4.0f;
inline constexpr glm::vec3  DEFAULT_EXTINCTION_COEFF    = glm::vec3(2.3, 1.5, 1.0);
inline constexpr glm::vec3  DEFAULT_SUN_DIR             = glm::vec3(-1, 2, 0.2);
inline constexpr f32        DEFAULT_STEP_SIZE           = 0.25f;

struct SimParams
{
    // bounds
    glm::uvec3  bounds;  // = grid res
    u32         gridPad; // padding to allow for 3x3x3 kernel support
    glm::vec3   invBounds;
    u32         _pad0;
    glm::uvec3  densityRes;
    f32         _pad1;
    glm::vec3   invDensityRes;
    u32         ncells;     // 64

    // physics params
    f32 gravity;
    f32 restDensity;
    f32 dynamicViscosity;
    f32 eosStiffness;
    f32 eosPower;   // 84
    f32 _pad2[3];   // 96

    // raymarch params
    glm::vec3   extinctionCoeff;
    f32         densityThreshold;
    glm::vec3   sunDir;
    f32         densityMultiplier;
    f32         shadowMultiplier;
    f32         shadowThreshold;
    f32         stepSize;
    f32         refractMultiplier;
};

struct SimResult
{
    Image* depth;
    Image* thickness;
    u32    stepsExecuted;
};

enum class GravityMode : int
{
    Normal = 0,
    Swirl = 1,
    Center = 2,

    MAX
};

enum class RenderMethod : int
{
    ScreenSpace = 0,
    Billboard = 1,

    MAX
};

void        SimulationInit(glm::uvec3 bounds);
Image*      SimulationAdvanceAndRenderImage(VkCommandBuffer cmd, f32 dt, VkDeviceAddress cameraData);
SimParams*  SimulationGetParams();
u32         SimulationGetParticleCount();
void        SimulationTogglePause();
