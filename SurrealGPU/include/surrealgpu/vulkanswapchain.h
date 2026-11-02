#pragma once

#include "vulkandevice.h"
#include "vulkanobjects.h"

class VulkanSemaphore;
class VulkanFence;

class VulkanSurfaceCapabilities
{
public:
	VkSurfaceCapabilitiesKHR Capabilites = { };
#ifdef WIN32
	VkSurfaceCapabilitiesFullScreenExclusiveEXT FullScreenExclusive = { VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT };
#else
	struct { void* pNext = nullptr; VkBool32 fullScreenExclusiveSupported = 0; } FullScreenExclusive;
#endif
	std::vector<VkPresentModeKHR> PresentModes;
	std::vector<VkSurfaceFormatKHR> Formats;
};

class VulkanSwapChain
{
public:
	VulkanSwapChain(VulkanDevice* device);
	~VulkanSwapChain();

	void Create(int width, int height, int imageCount, bool vsync, bool hdr, bool preferMailbox = false);
	bool Lost() const { return lost; }
	bool SurfaceLost() const { return surfaceLost; }

	int Width() const { return actualExtent.width; }
	int Height() const { return actualExtent.height; }
	bool MatchesSize(int width, int height) const { return requestedExtent.width == (uint32_t)width && requestedExtent.height == (uint32_t)height; }
	VkSurfaceTransformFlagBitsKHR Transform() const { return preTransform; }
	VkSurfaceFormatKHR Format() const { return format; }

	int ImageCount() const { return (int)images.size(); }
	VulkanImage* GetImage(int index) { return images[index].get(); }
	VulkanImageView* GetImageView(int index) { return views[index].get(); }

	int AcquireImage(VulkanSemaphore* semaphore = nullptr, VulkanFence* fence = nullptr);
	void QueuePresent(int imageIndex, VulkanSemaphore* semaphore = nullptr);

private:
	void DestroySwapchain(VkSwapchainKHR handle);
	bool framePacing = false;
	void SelectFormat(const VulkanSurfaceCapabilities& caps, bool hdr);

	bool CreateSwapchain(int width, int height, int imageCount, bool vsync, bool hdr, bool preferMailbox = false);

	VulkanSurfaceCapabilities GetSurfaceCapabilities();

	VulkanDevice* device = nullptr;
	bool lost = true;
	bool surfaceLost = false;

	VkExtent2D actualExtent = {};
	VkExtent2D requestedExtent = {};
	VkSurfaceTransformFlagBitsKHR preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkSurfaceFormatKHR format = {};
	VkPresentModeKHR presentMode = {};
	std::vector<std::unique_ptr<VulkanImage>> images;
	std::vector<std::unique_ptr<VulkanImageView>> views;

	VulkanSwapChain(const VulkanSwapChain&) = delete;
	VulkanSwapChain& operator=(const VulkanSwapChain&) = delete;
};
