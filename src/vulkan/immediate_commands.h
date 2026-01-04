#pragma once

#include <volk/volk.h>

VkCommandBuffer BeginImmediateCommands();
void SubmitImmediateCommands(VkCommandBuffer cmd);

