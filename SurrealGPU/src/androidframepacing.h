#pragma once
#ifdef __ANDROID__
#include "vulkandevice.h"
#include <surrealgpu/androidframepacingcontrol.h>
#include <swappy/swappyVk.h>
bool AndroidFramePacingRequested();
void AndroidFramePacingExtensions(VulkanDevice* device);
bool AndroidFramePacingInit(VulkanDevice* device, VkSwapchainKHR swapchain);
void AndroidFramePacingDestroy(VulkanDevice* device, VkSwapchainKHR swapchain);
#endif
