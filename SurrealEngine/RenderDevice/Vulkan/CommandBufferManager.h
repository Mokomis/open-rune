#pragma once

#include <surrealgpu/vulkanobjects.h>

class VulkanRenderDevice;

class CommandBufferManager
{
public:
	CommandBufferManager(VulkanRenderDevice* renderer);
	~CommandBufferManager();

	void WaitForTransfer();
	void WaitForPending();
	void SubmitCommands(bool present, int presentWidth, int presentHeight, bool presentFullscreen, bool wait);
	VulkanCommandBuffer* GetTransferCommands();
	VulkanCommandBuffer* GetDrawCommands();
	void DeleteFrameObjects();

	struct DeleteList
	{
		std::vector<std::unique_ptr<VulkanImage>> images;
		std::vector<std::unique_ptr<VulkanImageView>> imageViews;
		std::vector<std::unique_ptr<VulkanBuffer>> buffers;
		std::vector<std::unique_ptr<VulkanDescriptorSet>> descriptors;
	};
	std::unique_ptr<DeleteList> FrameDeleteList;

	std::shared_ptr<VulkanSwapChain> SwapChain;
	int PresentImageIndex = -1;
	bool UsingVsync = false;
	bool UsingHdr = false;
	bool UsingMailbox = false;

private:
	VulkanRenderDevice* renderer = nullptr;

	std::unique_ptr<VulkanSemaphore> ImageAvailableSemaphore;
	std::vector<std::unique_ptr<VulkanSemaphore>> RenderFinishedSemaphores;
	std::unique_ptr<VulkanSemaphore> TransferSemaphore;
	std::unique_ptr<VulkanFence> RenderFinishedFence;
	std::unique_ptr<VulkanCommandPool> CommandPool;
	std::unique_ptr<VulkanCommandBuffer> DrawCommands;
	std::unique_ptr<VulkanCommandBuffer> TransferCommands;
	// Keep submitted commands and their temporary resources alive while the CPU
	// records the next frame. Only one submission is executing on the GPU.
	std::unique_ptr<VulkanCommandBuffer> PendingDrawCommands;
	std::unique_ptr<VulkanCommandBuffer> PendingTransferCommands;
	std::unique_ptr<DeleteList> PendingDeleteList;
	bool SubmissionPending = false;
	bool PendingPresent = false;
	bool PendingGpuSample = false;
	double PendingCpuMs[5] = {};
	int ProfileFrames = 0;
	int ProfileFrame = 0;
	double ProfileCpuTotals[6] = {};
	double ProfileGpuTotals[4] = {};
	int ProfileGpuSamples = 0;
	std::unique_ptr<VulkanQueryPool> ProfileTimestamps;
	uint32_t ProfileTimestampBits = 0;
};
