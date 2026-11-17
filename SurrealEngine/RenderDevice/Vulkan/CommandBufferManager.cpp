
#include "Precomp.h"
#include "CommandBufferManager.h"
#include "VulkanRenderDevice.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <chrono>
#include <surrealgpu/vulkanswapchain.h>
#include <surrealgpu/vulkansurface.h>
#include <surrealwidgets/core/widget.h>

CommandBufferManager::CommandBufferManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
	SwapChain = VulkanSwapChainBuilder()
		.Create(renderer->Device.get());

	ImageAvailableSemaphore = SemaphoreBuilder()
		.DebugName("ImageAvailableSemaphore")
		.Create(renderer->Device.get());

	RenderFinishedFence = FenceBuilder()
		.DebugName("RenderFinishedFence")
		.Create(renderer->Device.get());

	TransferSemaphore.reset(new VulkanSemaphore(renderer->Device.get()));

	CommandPool = CommandPoolBuilder()
		.QueueFamily(renderer->Device.get()->GraphicsFamily)
		.DebugName("CommandPool")
		.Create(renderer->Device.get());

	FrameDeleteList = std::make_unique<DeleteList>();
	ProfileFrames = commandline->GetArgInt("", "--profile-frames", 0);
	ProfilePasses = ProfileFrames > 0 && commandline->GetArgInt("", "--profile-passes", 0) != 0;
	if (ProfileFrames > 0)
	{
		uint32_t count = 0;
		auto physical = renderer->Device->PhysicalDevice.Device;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
		ProfileTimestampBits = families.at(renderer->Device->GraphicsFamily).timestampValidBits;
		if (ProfileTimestampBits)
			ProfileTimestamps = QueryPoolBuilder().QueryType(VK_QUERY_TYPE_TIMESTAMP, ProfilePasses ? MaxProfileQueries : 4).Create(renderer->Device.get());
	}
}

CommandBufferManager::~CommandBufferManager()
{
	DeleteFrameObjects();
}

void CommandBufferManager::WaitForTransfer()
{
	WaitForPending();
	renderer->Uploads->SubmitUploads();

	if (TransferCommands)
	{
		TransferCommands->end();

		QueueSubmit()
			.AddCommandBuffer(TransferCommands.get())
			.Execute(renderer->Device.get(), renderer->Device.get()->GraphicsQueue, RenderFinishedFence.get());

		VkResult result = vkWaitForFences(renderer->Device.get()->device, 1, &RenderFinishedFence->fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkWaitForFences failed");
		result = vkResetFences(renderer->Device.get()->device, 1, &RenderFinishedFence->fence);
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkResetFences failed");

		TransferCommands.reset();
	}
}

void CommandBufferManager::SubmitCommands(bool present, int presentWidth, int presentHeight, bool presentFullscreen, bool wait)
{
	// Drain the previous GPU submission only after the CPU has prepared this
	// frame in its alternate vertex/index/upload buffers. Shared scene images
	// and descriptors therefore never have concurrent GPU users.
	WaitForPending();
	renderer->DescriptorSets->UpdateBindlessSet();
	auto profileStart = std::chrono::steady_clock::now();
	renderer->Uploads->SubmitUploads();
	auto profileUploads = std::chrono::steady_clock::now();
	auto profileAcquire = profileUploads;

	if (present)
	{
		if (SwapChain->Lost() || !SwapChain->MatchesSize(presentWidth, presentHeight) || UsingVsync != renderer->UseVSync || UsingHdr != renderer->Hdr || UsingMailbox != renderer->PreferMailbox)
		{
			// A render fence does not guarantee that presentation has consumed its
			// semaphore. Drain presentation before replacing the swapchain objects.
			vkDeviceWaitIdle(renderer->Device->device);
			UsingVsync = renderer->UseVSync;
			UsingHdr = renderer->Hdr;
			UsingMailbox = renderer->PreferMailbox;
			renderer->Framebuffers->DestroySwapChainFramebuffers();
			if (SwapChain->SurfaceLost())
			{
				// Android replaces its native window after background/resume. The
				// old surface cannot be reused even with a new swapchain. Retire
				// all its presentation objects before releasing that surface.
				SwapChain.reset();
				auto instance = renderer->Device->Surface->Instance;
				renderer->Device->Surface = std::make_shared<VulkanSurface>(instance,
					renderer->Viewport->CreateVulkanSurface(instance->Instance));
				SwapChain = VulkanSwapChainBuilder().Create(renderer->Device.get());
				LogMessage("RUNE_VULKAN_SURFACE recreated=1");
			}
			int imageCount = renderer->UseVSync ? 2 : 3;
#ifdef __ANDROID__
			// Diagnostic override for presentation-buffer comparisons. Keep the
			// existing default: requesting three did not improve this device's run.
			imageCount = clamp(commandline->GetArgInt("", "--swapchain-images", 2), 2, 4);
#endif
			SwapChain->Create(presentWidth, presentHeight, imageCount, renderer->UseVSync, renderer->Hdr, renderer->PreferMailbox);
			renderer->Framebuffers->CreateSwapChainFramebuffers();

			RenderFinishedSemaphores.clear();
			for (int i = 0; i < SwapChain->ImageCount(); i++)
			{
				RenderFinishedSemaphores.push_back(SemaphoreBuilder()
					.DebugName("RenderFinishedSemaphore")
					.Create(renderer->Device.get()));
			}
		}

		PresentImageIndex = SwapChain->AcquireImage(ImageAvailableSemaphore.get());
		profileAcquire = std::chrono::steady_clock::now();
		if (PresentImageIndex != -1)
		{
			if (ProfileTimestamps) GetDrawCommands()->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ProfileTimestamps.get(), 2);
			renderer->DrawPresentTexture(SwapChain->Width(), SwapChain->Height());
		}
	}
	auto profileEncode = std::chrono::steady_clock::now();

	if (TransferCommands)
	{
		TransferCommands->end();

		QueueSubmit()
			.AddCommandBuffer(TransferCommands.get())
			.AddSignal(TransferSemaphore.get())
			.Execute(renderer->Device.get(), renderer->Device.get()->GraphicsQueue);
	}

	bool gpuSample = present && DrawCommands && ProfileTimestamps;
	if (DrawCommands)
	{
		if (ProfileTimestamps) DrawCommands->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ProfileTimestamps.get(), 3);
		DrawCommands->end();
	}

	QueueSubmit submit;
	if (DrawCommands)
	{
		submit.AddCommandBuffer(DrawCommands.get());
	}
	if (TransferCommands)
	{
		submit.AddWait(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, TransferSemaphore.get());
	}
	if (present && PresentImageIndex != -1)
	{
		submit.AddWait(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, ImageAvailableSemaphore.get());
		submit.AddSignal(RenderFinishedSemaphores[PresentImageIndex].get());
	}
	submit.Execute(renderer->Device.get(), renderer->Device.get()->GraphicsQueue, RenderFinishedFence.get());
	auto profileSubmit = std::chrono::steady_clock::now();

	if (present && PresentImageIndex != -1)
	{
		SwapChain->QueuePresent(PresentImageIndex, RenderFinishedSemaphores[PresentImageIndex].get());
	}
	auto profilePresent = std::chrono::steady_clock::now();

	auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
	PendingCpuMs[0] = ms(profileStart, profileUploads);
	PendingCpuMs[1] = ms(profileUploads, profileAcquire);
	PendingCpuMs[2] = ms(profileAcquire, profileEncode);
	PendingCpuMs[3] = ms(profileEncode, profileSubmit);
	PendingCpuMs[4] = ms(profileSubmit, profilePresent);
	PendingPresent = present;
	PendingGpuSample = gpuSample && PresentImageIndex != -1;
	PendingQueryCount = ProfileQueryCount;
	PendingPassComplete = ProfilePasses && !PassOverflow && !HadPartialSubmit && present;
	if (ProfilePasses && DrawCommands)
	{
		PassIntervals.push_back({PassBegin, 3, CurrentPass});
		PendingPassIntervals = std::move(PassIntervals);
	}
	HadPartialSubmit = !present;
	PendingDrawCommands = std::move(DrawCommands);
	PendingTransferCommands = std::move(TransferCommands);
	PendingDeleteList = std::move(FrameDeleteList);
	FrameDeleteList = std::make_unique<DeleteList>();
	SubmissionPending = true;
	if (wait)
		WaitForPending();
}

void CommandBufferManager::WaitForPending()
{
	if (!SubmissionPending)
		return;

	auto start = std::chrono::steady_clock::now();
	VkResult result = vkWaitForFences(renderer->Device->device, 1, &RenderFinishedFence->fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	if (result != VK_SUCCESS)
		throw std::runtime_error("vkWaitForFences failed");
	double fenceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
	double gpuMs[4] = {};
	bool gpuSample = false;
	if (PendingGpuSample)
	{
		uint64_t timestamps[MaxProfileQueries] = {};
		uint32_t count = ProfilePasses ? PendingQueryCount : 4;
		if (ProfileTimestamps->getResults(0, count, count * sizeof(uint64_t), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT))
		{
			uint64_t mask = ProfileTimestampBits == 64 ? UINT64_MAX : (uint64_t(1) << ProfileTimestampBits) - 1;
			auto ms = [&](int a, int b) { return ((timestamps[b] - timestamps[a]) & mask) * (double)renderer->Device->PhysicalDevice.Properties.Properties.limits.timestampPeriod / 1000000.0; };
			gpuMs[0] = ms(0, 3);
			gpuMs[1] = ms(0, 1);
			gpuMs[2] = ms(1, 2);
			gpuMs[3] = ms(2, 3);
			gpuSample = true;
			if (ProfilePasses && ProfileFrame >= 60 && ProfileFrame < 60 + ProfileFrames)
			{
				if (PendingPassComplete)
				{
					for (const auto& interval : PendingPassIntervals)
						PassTotals[static_cast<int>(interval.pass)] += ms(interval.begin, interval.end);
					++PassSamples;
				}
				else ++PassDropped;
			}
		}
	}
	result = vkResetFences(renderer->Device->device, 1, &RenderFinishedFence->fence);
	if (result != VK_SUCCESS)
		throw std::runtime_error("vkResetFences failed");

	PendingDrawCommands.reset();
	PendingTransferCommands.reset();
	PendingDeleteList.reset();
	SubmissionPending = false;
	if (PendingPresent && ProfileFrames > 0)
	{
		++ProfileFrame;
		if (ProfileFrame > 60 && ProfileFrame <= 60 + ProfileFrames)
		{
			for (int i = 0; i < 5; ++i) ProfileCpuTotals[i] += PendingCpuMs[i];
			ProfileCpuTotals[5] += fenceMs;
			if (gpuSample)
			{
				for (int i = 0; i < 4; ++i) ProfileGpuTotals[i] += gpuMs[i];
				++ProfileGpuSamples;
			}
			if (ProfileFrame == 60 + ProfileFrames)
			{
				LogMessage("RUNE_VULKAN_PROFILE uploads_ms=" + std::to_string(ProfileCpuTotals[0]/ProfileFrames) + " acquire_ms=" + std::to_string(ProfileCpuTotals[1]/ProfileFrames) + " encode_ms=" + std::to_string(ProfileCpuTotals[2]/ProfileFrames) + " submit_ms=" + std::to_string(ProfileCpuTotals[3]/ProfileFrames) + " present_ms=" + std::to_string(ProfileCpuTotals[4]/ProfileFrames) + " fence_ms=" + std::to_string(ProfileCpuTotals[5]/ProfileFrames) + " gpu_interval_ms=" + std::to_string(ProfileGpuSamples ? ProfileGpuTotals[0]/ProfileGpuSamples : -1) + " gpu_wait_ms=" + std::to_string(ProfileGpuSamples ? ProfileGpuTotals[1]/ProfileGpuSamples : -1) + " gpu_scene_ms=" + std::to_string(ProfileGpuSamples ? ProfileGpuTotals[2]/ProfileGpuSamples : -1) + " gpu_present_ms=" + std::to_string(ProfileGpuSamples ? ProfileGpuTotals[3]/ProfileGpuSamples : -1) + " gpu_samples=" + std::to_string(ProfileGpuSamples));
				if (ProfilePasses)
				{
					const char* names[] = {"other", "scene", "shadow", "water", "resolve", "bloom", "present"};
					std::string report = "RUNE_GPU_PASSES samples=" + std::to_string(PassSamples) + " dropped=" + std::to_string(PassDropped);
					for (int i=0; i<static_cast<int>(GpuPass::Count); ++i)
						report += " " + std::string(names[i]) + "_ms=" + std::to_string(PassSamples ? PassTotals[i]/PassSamples : -1);
					LogMessage(report);
				}
			}
		}
	}
}

VulkanCommandBuffer* CommandBufferManager::GetTransferCommands()
{
	if (!TransferCommands)
	{
		TransferCommands = CommandPool->createBuffer();
		TransferCommands->begin();
	}
	return TransferCommands.get();
}

VulkanCommandBuffer* CommandBufferManager::GetDrawCommands()
{
	if (!DrawCommands)
	{
		DrawCommands = CommandPool->createBuffer();
		DrawCommands->begin();
		if (ProfileTimestamps)
		{
			DrawCommands->resetQueryPool(ProfileTimestamps.get(), 0, ProfilePasses ? MaxProfileQueries : 4);
			DrawCommands->writeTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ProfileTimestamps.get(), 0);
			DrawCommands->writeTimestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, ProfileTimestamps.get(), 1);
			ProfileQueryCount = 4; PassBegin = 0; CurrentPass = GpuPass::Other;
			PassOverflow = false; PassIntervals.clear();
		}
	}
	return DrawCommands.get();
}

std::unique_ptr<VulkanCommandBuffer> CommandBufferManager::BeginSceneCommands(VulkanRenderPass* pass, VulkanFramebuffer* framebuffer)
{
	auto commands = CommandPool->createBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	VkCommandBufferInheritanceInfo inheritance = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
	inheritance.renderPass = pass->renderPass;
	inheritance.framebuffer = framebuffer->framebuffer;
	commands->begin(&inheritance);
	return commands;
}

void CommandBufferManager::SetProfilePass(GpuPass pass)
{
	if (!ProfilePasses || !ProfileTimestamps) return;
	auto* commands = GetDrawCommands();
	if (CurrentPass == pass) return;
	if (ProfileQueryCount == MaxProfileQueries) { PassOverflow = true; return; }
	uint32_t query = ProfileQueryCount++;
	commands->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ProfileTimestamps.get(), query);
	PassIntervals.push_back({PassBegin, query, CurrentPass});
	PassBegin = query; CurrentPass = pass;
}

void CommandBufferManager::DeleteFrameObjects()
{
	FrameDeleteList = std::make_unique<DeleteList>();
}
