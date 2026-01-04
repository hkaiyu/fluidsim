#pragma once

#include <volk/volk.h>
#include <glm/glm.hpp>

#include "core/utils.h"
#include "simulation.h"

struct FrameStats
{
    u32 simSteps;
    u32 particleCount;
};

// TODO: overlay by tab (add arg for that), but for now, all tweaks will just be added to one tab on settings window
void OverlayAddCombo(const char* pName, int* pCurr, std::initializer_list<const char *> items);
void OverlayAddCheckbox(const char* pName, bool* pBool);
void OverlayAddSliderFloat(const char* pName, f32* pFloat, f32 min, f32 max);
void OverlayAddSliderFloat3(const char* pName, f32* pFloat3, f32 min, f32 max);

void OverlayInit();

void OverlayRender(VkCommandBuffer cmd, const FrameStats& stats);

