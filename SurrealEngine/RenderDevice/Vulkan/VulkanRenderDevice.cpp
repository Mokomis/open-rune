
#include "Precomp.h"
#include "VulkanRenderDevice.h"
#include "ShaderOptimizationDefaults.h"
#include "RenderDevice/VisualComparison.h"
#include "CachedTexture.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <surrealgpu/vulkanbuilders.h>
#include <surrealgpu/vulkanswapchain.h>
#include <surrealgpu/vulkansurface.h>
#include <surrealwidgets/core/widget.h>
#include <cmath>
#include "ShadowReceiverBounds.h"
#include "Light/LightSystem.h"
#include "Engine.h"
#include "RuneMenu.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include <cstring>
#include <stdexcept>
#include "Package/Package.h"
#include <cctype>
#include <bit>
#include "CrestWater.h"
#include <fstream>

VulkanRenderDevice::VulkanRenderDevice(Widget* InViewport)
{
	Viewport = InViewport;

	try
	{
		UseDebugLayer = UseDebugLayer || commandline->HasArg("", "--vulkan-validation");
		std::shared_ptr<VulkanInstance> instance = VulkanInstanceBuilder()
			.RequireExtensions(Viewport->GetVulkanInstanceExtensions())
			.OptionalSwapchainColorspace()
			.DebugLayer(UseDebugLayer)
			.Create();
		if (UseDebugLayer)
		{
			if (!instance->DebugLayerActive)
				throw std::runtime_error("Requested Vulkan validation layer is unavailable");
			LogMessage("RUNE_VULKAN_VALIDATION active=1 synchronization=1");
		}

		auto surface = std::make_shared<VulkanSurface>(instance, Viewport->CreateVulkanSurface(instance->Instance));
		if (!surface)
			throw std::runtime_error("No vulkan surface found");

		auto deviceBuilder = VulkanDeviceBuilder();
		deviceBuilder.OptionalDescriptorIndexing();
		deviceBuilder.OptionalRayQuery();
		deviceBuilder.Surface(surface);
		deviceBuilder.RequireExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		deviceBuilder.RequireExtension(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
		deviceBuilder.SelectDevice(VkDeviceIndex);
		Device = deviceBuilder.Create(surface->Instance);

		bool supportsBindless =
			Device->EnabledFeatures.DescriptorIndexing.descriptorBindingPartiallyBound &&
			Device->EnabledFeatures.DescriptorIndexing.runtimeDescriptorArray &&
			Device->EnabledFeatures.DescriptorIndexing.shaderSampledImageArrayNonUniformIndexing;

		if (!supportsBindless)
			throw std::runtime_error("VulkanDrv requires a GPU that supports bindless textures!");

        // Packed positive HDR halves scene/bloom bandwidth. Android is validated
        // with it; other platforms keep their existing format until tested.
#ifdef __ANDROID__
        constexpr int packedHdrDefault=1;
#else
        constexpr int packedHdrDefault=0;
#endif
        if(commandline->GetArgInt("","--render-packed-hdr",packedHdrDefault)!=0) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(Device->PhysicalDevice.Device,VK_FORMAT_B10G11R11_UFLOAT_PACK32,&properties);
            const auto required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT|
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|
                VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
            VkImageFormatProperties image{};
            auto supported=vkGetPhysicalDeviceImageFormatProperties(Device->PhysicalDevice.Device,VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&image);
            const auto& limits=Device->PhysicalDevice.Properties.Properties.limits;
            // Preserve every AA count selectable by SceneTextures, including
            // higher desktop counts when explicitly opting into this path.
            const auto samples=limits.sampledImageColorSampleCounts & limits.sampledImageDepthSampleCounts & limits.sampledImageStencilSampleCounts;
            if(supported==VK_SUCCESS && (properties.optimalTilingFeatures&required)==required &&
                (image.sampleCounts&samples)==samples)
                SceneColorFormat=VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        }
        LogMessage("RUNE_PACKED_HDR enabled="+std::to_string(SceneColorFormat==VK_FORMAT_B10G11R11_UFLOAT_PACK32));
        // These transforms and contact-light values are shared by every draw.
        // A uniform buffer avoids repeated general-purpose storage-buffer reads.
#ifdef __ANDROID__
        constexpr int uniformShadowDefault=1;
#else
        constexpr int uniformShadowDefault=0;
#endif
        UniformShadowData=commandline->GetArgInt("","--render-uniform-shadow-data",uniformShadowDefault)!=0;
        LogMessage("RUNE_UNIFORM_SHADOW_DATA enabled="+std::to_string(UniformShadowData));
		Commands.reset(new CommandBufferManager(this));
		Samplers.reset(new SamplerManager(this));
		Textures.reset(new TextureManager(this));
		Buffers.reset(new BufferManager(this));
		int defaultOverlap = 0;
#ifdef __ANDROID__
		defaultOverlap = 1;
#endif
		PreferMailbox = commandline->GetArgInt("", "--render-mailbox", defaultOverlap) != 0;
        CullShadowReceivers=commandline->GetArgInt("","--render-shadow-receivers",1)!=0;
        LogMessage("RUNE_SHADOW_RECEIVERS enabled="+std::to_string(CullShadowReceivers));
        GroupShadowPasses=commandline->GetArgInt("","--render-group-shadows",0)!=0;
        LogMessage("RUNE_SHADOW_GROUPING enabled="+std::to_string(GroupShadowPasses));
        // Qualified tile-renderer paths avoid a separate MSAA color resolve/store.
        // Keep unqualified devices and drivers on their previous defaults.
        const int inPassResolveDefault = UsesQualifiedShaderDefaults(*Device) ? 1 : 0;
        InPassResolve = commandline->GetArgInt("", "--render-inpass-resolve", inPassResolveDefault) != 0;
		LogMessage("RUNE_VULKAN_INPASS_RESOLVE enabled=" + std::to_string(InPassResolve));
		OmitUnusedHit = commandline->GetArgInt("", "--render-omit-hit", defaultOverlap) != 0;
		LogMessage("RUNE_VULKAN_OMIT_HIT enabled=" + std::to_string(OmitUnusedHit));
		DiscardUnusedHit = commandline->GetArgInt("", "--render-discard-hit", 0) != 0;
		LogMessage("RUNE_VULKAN_DISCARD_HIT enabled=" + std::to_string(DiscardUnusedHit));
		OverlapFrames = commandline->GetArgInt("", "--render-overlap", defaultOverlap) != 0;
		if (OverlapFrames)
			AlternateBuffers = std::make_unique<BufferManager>(this);
		LogMessage("RUNE_VULKAN_OVERLAP enabled=" + std::to_string(OverlapFrames));
		Shaders.reset(new ShaderManager(this));
		Uploads.reset(new UploadManager(this));
		CreateShadowImage();
        CreateWaterImage();
        if(commandline->GetArgInt("","--water-model",1)==3) {
            Crest=std::make_unique<CrestWater>(Device.get());
            LogMessage("RUNE_CREST_NATIVE initialized=1 version=4.23.0 fft=128 cascades=16 unity_runtime=0");
        }
		DescriptorSets.reset(new DescriptorSetManager(this));
		SceneStoreOverride = clamp(commandline->GetArgInt("", "--render-scene-stores", -1), -1, 2);
#ifdef __ANDROID__
		QualifiedSceneStores = UsesQualifiedShaderDefaults(*Device);
#endif
		ProfileSceneStores = commandline->HasArg("", "--frame-benchmark");
		RenderPasses.reset(new RenderPassManager(this));
		Framebuffers.reset(new FramebufferManager(this));
        CreateShadowPass();

		const auto& props = Device->PhysicalDevice.Properties.Properties;

		std::string deviceType;
		switch (props.deviceType)
		{
		case VK_PHYSICAL_DEVICE_TYPE_OTHER: deviceType = "other"; break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceType = "integrated gpu"; break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: deviceType = "discrete gpu"; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: deviceType = "virtual gpu"; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: deviceType = "cpu"; break;
		default: deviceType = std::to_string((int)props.deviceType); break;
		}

		std::string apiVersion = std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_PATCH(props.apiVersion));
		std::string driverVersion = std::to_string(VK_VERSION_MAJOR(props.driverVersion)) + "." + std::to_string(VK_VERSION_MINOR(props.driverVersion)) + "." + std::to_string(VK_VERSION_PATCH(props.driverVersion));

		LogMessage(std::string("Vulkan device: ") + props.deviceName);
#ifdef __ANDROID__
        VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &driver;
        vkGetPhysicalDeviceProperties2(Device->PhysicalDevice.Device, &properties);
        std::fprintf(stderr, "RUNE_VULKAN_DRIVER gpu=%s id=%d name=%s info=%s\n",
            props.deviceName, int(driver.driverID), driver.driverName, driver.driverInfo);
        const char* requested = std::getenv("OPENRUNE_VULKAN_DRIVER");
        if (requested && std::strcmp(requested, "turnip") == 0 && driver.driverID != VK_DRIVER_ID_MESA_TURNIP)
            Exception::Throw("Turnip requested but a different driver was loaded");
#endif
		LogMessage("Vulkan device type: " + deviceType);
		LogMessage("Vulkan version: " + apiVersion + " (api) " + driverVersion + " (driver)");
	}
	catch (const std::exception& e)
	{
		Exception::Throw(std::string("Could not create vulkan renderer: ") + e.what());
	}
}

VulkanRenderDevice::~VulkanRenderDevice()
{
	if (Commands)
		Commands->WaitForPending();
	if (Device)
		vkDeviceWaitIdle(Device->device);

	WaterFramebuffer.reset(); WaterColorView.reset(); WaterDepthView.reset(); WaterHitView.reset();
    WaterColor.reset(); WaterDepth.reset(); WaterHit.reset();
    ShadowFramebuffer.reset(); ShadowPipeline.Pipeline.reset();
    ShadowPass.reset(); ShadowPassContinue.reset();
    Framebuffers.reset();
	RenderPasses.reset();
	DescriptorSets.reset();
	Uploads.reset();
	Shaders.reset();
	Buffers.reset();
	AlternateBuffers.reset();
	Textures.reset();
	Samplers.reset();
	RecordedScene.reset();
	if (ProfileSceneStores)
		LogMessage("RUNE_SCENE_STORES mode=" + std::to_string(SceneStoreMode) +
			" recorded=" + std::to_string(RecordedSceneSegments) +
			" color_discard=" + std::to_string(DiscardedSceneColor) +
			" depth_discard=" + std::to_string(DiscardedSceneDepth));
	Commands.reset();

    Crest.reset();
	WaterView.reset(); WaterImage.reset();
    ShadowView.reset(); ShadowImage.reset(); ShadowSampler.reset();
	Device.reset();
}

void VulkanPrintLog(const char* typestr, const std::string& msg)
{
	LogMessage(std::string("[") + typestr + "] " + msg);
}

void VulkanError(const char* text)
{
	throw std::runtime_error(text);
}

void VulkanRenderDevice::SubmitFrame(bool present, int presentWidth, int presentHeight, bool presentFullscreen)
{
	Commands->SubmitCommands(present, presentWidth, presentHeight, presentFullscreen, !OverlapFrames || !present || HitData);
	if (present && OverlapFrames)
		Buffers.swap(AlternateBuffers);

	Batch.SceneIndexStart = 0;
	Buffers->MeshLightCount = 0;
	SceneVertexPos = 0;
	SceneIndexPos = 0;
}

void VulkanRenderDevice::Flush(bool AllowPrecache)
{
	if (IsLocked)
	{
        if (ScenePassDeferred && !DrawingShadow && !WaterReflectionActive) StartScenePass();
		DrawBatch(Commands->GetDrawCommands());
		EndSceneSegment();
		SubmitFrame(false, 0, 0, false);

		ClearTextureCache();
        if(DrawingShadow){BeginShadowPass(true);return;}
        if(WaterReflectionActive){BeginWaterPass(true);return;}

		ResumeScenePass();
	}
	else
	{
		ClearTextureCache();
	}

	if (AllowPrecache && UsePrecache)
		PrecacheOnFlip = 1;
}

void VulkanRenderDevice::Lock(vec4 InFlashScale, vec4 InFlashFog, vec4 ScreenClear, uint8_t* InHitData, int* InHitSize)
{
	*Buffers->ShadowLighting = {};
    Buffers->ShadowLighting->CameraPosition.w = (WorldFeatures & 512) ? 1.0f : 0.0f;
    if (!ShadowInitialized)
    {
        PipelineBarrier().AddImage(ShadowImage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
            .Execute(Commands->GetDrawCommands(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        ShadowInitialized = true;
    }
    if (!WaterInitialized) {
        PipelineBarrier().AddImage(WaterImage.get(),VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,0,VK_ACCESS_SHADER_READ_BIT)
            .Execute(Commands->GetDrawCommands(),VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        WaterInitialized=true;
    }
    if(Crest && !Crest->Initialized) {
        Commands->SetProfilePass(CommandBufferManager::GpuPass::Water);
        Crest->Update(Commands->GetDrawCommands(),WaterViewTime);
        if(Crest->Frames==1 || (commandline->HasArg("","--trace-rune-water") && Crest->Frames%120==0))
            LogMessage("RUNE_CREST_FFT frame="+std::to_string(Crest->Frames)+" time="+std::to_string(WaterViewTime));
    }
    HitData = InHitData;
	DiscardHitThisFrame = DiscardUnusedHit && !InHitData;
	OmitHitThisFrame = OmitUnusedHit && !InHitData;
	HitSize = InHitSize;

    if(commandline->HasArg("","--trace-rune-water") && Textures->Scene) {
        static int width=0,height=0,samples=0;
        if(width!=Textures->Scene->Width || height!=Textures->Scene->Height || samples!=int(Textures->Scene->SceneSamples)) {
            width=Textures->Scene->Width;height=Textures->Scene->Height;samples=int(Textures->Scene->SceneSamples);
            LogMessage("RUNE_WATER_TARGET width="+std::to_string(width)+" height="+std::to_string(height)+" samples="+std::to_string(samples));
        }
    }
	FlashScale = InFlashScale;
	FlashFog = InFlashFog;

	pushconstants.hitIndex = 0;
	ForceHitIndex = -1;

	// If frame textures no longer match the window or user settings, recreate them along with the swap chain
	int renderWidth = GetRenderWidth();
	int renderHeight = GetRenderHeight();
	if (!Textures->Scene || Textures->Scene->Width != renderWidth || Textures->Scene->Height != renderHeight || Textures->Scene->Multisample != GetSettingsMultisample())
	{
		Commands->WaitForPending();
		Framebuffers->DestroySceneFramebuffer();
        WaterFramebuffer.reset(); CachedWaterValid=false;
		Textures->Scene.reset();
		Textures->Scene.reset(new SceneTextures(this, renderWidth, renderHeight, GetSettingsMultisample()));
#ifdef __ANDROID__
		LogMessage("ANDROID_RENDER_TARGET size=" + std::to_string(renderWidth) + "x" + std::to_string(renderHeight) + " scale=" + std::to_string(ResolutionScale) + " samples=" + std::to_string(Textures->Scene->SceneSamples));
#endif
		// Native OPPO comparisons favor the final-store optimization at 4x.
		// Other sample counts and drivers keep their original recording path.
		const bool defaultDiscard = QualifiedSceneStores && UsesInPassResolve() &&
			Textures->Scene->SceneSamples == VK_SAMPLE_COUNT_4_BIT;
		SceneStoreMode = SceneStoreOverride >= 0 ? SceneStoreOverride : int(defaultDiscard);
		RenderPasses->CreateRenderPass();
		RenderPasses->CreatePipelines();
		Framebuffers->CreateSceneFramebuffer();
		DescriptorSets->UpdateFrameDescriptors();
	}

	auto cmdbuffer = Commands->GetDrawCommands();
    if(Crest)Crest->InitializeCapture(cmdbuffer);

	// Special thanks to Khronos and AMD for making this absolute hell to use.
	VkAccessFlags srcColorAccess = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	VkAccessFlags dstColorAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	VkAccessFlags srcDepthAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	VkAccessFlags dstDepthAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

	PipelineBarrier()
		.AddImage(Textures->Scene->ColorBuffer.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcColorAccess, dstColorAccess)
		.AddImage(Textures->Scene->HitBuffer.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcColorAccess, dstColorAccess)
		.AddImage(Textures->Scene->DepthBuffer.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, srcDepthAccess, dstDepthAccess, VK_IMAGE_ASPECT_DEPTH_BIT)
		.Execute(cmdbuffer, srcStages, dstStages);

    DeferredScreenClear = ScreenClear;
    // On tile GPUs an empty MSAA color pass before the shadow pass needlessly
    // stores/reloads the full native color and depth targets. Delay it until
    // after shadows unless a PreRender script actually draws something.
    ScenePassDeferred = true;
    if (!(WorldFeatures & (512|2048))) StartScenePass();
    IsLocked = true;
}

void VulkanRenderDevice::StartScenePass()
{
	BeginSceneSegment(false);
}

void VulkanRenderDevice::BeginSceneRenderPass(VulkanCommandBuffer* commands, VulkanRenderPass* pass, VkSubpassContents contents)
{
	PrepareResolveTarget(commands, SceneContinuation);
	RenderPassBegin()
		.RenderPass(pass)
		.Framebuffer(Framebuffers->GetSceneFramebuffer(OmitHitThisFrame))
		.RenderArea(0, 0, Textures->Scene->Width, Textures->Scene->Height)
		.AddClearColor(DeferredScreenClear.x, DeferredScreenClear.y, DeferredScreenClear.z, DeferredScreenClear.w)
		.AddClearColor(0.0f, 0.0f, 0.0f, 0.0f)
		.AddClearDepthStencil(1.0f, 0)
		.Execute(commands, contents);
}

void VulkanRenderDevice::BindSceneBuffers(VulkanCommandBuffer* commands)
{
	VkBuffer vertexBuffers[] = { Buffers->SceneVertexBuffer->buffer };
	VkDeviceSize offsets[] = { 0 };
	commands->bindVertexBuffers(0, 1, vertexBuffers, offsets);
	commands->bindIndexBuffer(Buffers->SceneIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
}

void VulkanRenderDevice::BeginSceneSegment(bool continuation)
{
	Commands->SetProfilePass(CommandBufferManager::GpuPass::Scene);
	SceneContinuation = continuation;
	auto* pass = RenderPasses->GetScenePass(continuation, DiscardHitThisFrame, OmitHitThisFrame);
	if (SceneStoreMode && OmitHitThisFrame)
	{
		// Record a segment before deciding whether another pass will load it.
		// A water capture or an overflow flush keeps the original store ops.
		RecordedScene = Commands->BeginSceneCommands(pass, Framebuffers->GetSceneFramebuffer(true));
		BindSceneBuffers(RecordedScene.get());
		if (viewportdesc.width > 0.0f && viewportdesc.height > 0.0f)
			RecordedScene->setViewport(0, 1, &viewportdesc);
	}
	else
	{
		auto* commands = Commands->GetDrawCommands();
		BeginSceneRenderPass(commands, pass, VK_SUBPASS_CONTENTS_INLINE);
		BindSceneBuffers(commands);
		if (continuation) commands->setViewport(0, 1, &viewportdesc);
	}
	ScenePassDeferred = false;
}

void VulkanRenderDevice::EndSceneSegment(bool final)
{
	auto* commands = Commands->GetDrawCommands();
	if (RecordedScene)
	{
		auto* pass = RenderPasses->GetScenePass(SceneContinuation, DiscardHitThisFrame, OmitHitThisFrame);
		if (final && SceneStoreMode == 1)
		{
			const bool needsDepth = AmbientOcclusionMode() != 0;
			auto* optimized = needsDepth ? RenderPasses->Scene.FinalNoHitKeepDepth[SceneContinuation].get() :
				RenderPasses->Scene.FinalNoHit[SceneContinuation].get();
			if (optimized)
			{
				pass = optimized;
				DiscardedSceneColor += UsesInPassResolve();
				DiscardedSceneDepth += !needsDepth;
			}
		}

		RecordedScene->end();
		BeginSceneRenderPass(commands, pass, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		commands->executeCommands(1, &RecordedScene->buffer);
		Commands->FrameDeleteList->sceneCommands.push_back(std::move(RecordedScene));
		++RecordedSceneSegments;
	}
	commands->endRenderPass();
}

void VulkanRenderDevice::FlushDrawBatchAndWait()
{
    if (ScenePassDeferred && !DrawingShadow && !WaterReflectionActive) StartScenePass();
	DrawBatch(Commands->GetDrawCommands());
	EndSceneSegment();
	SubmitFrame(false, 0, 0, false);

	if (DrawingShadow) { BeginShadowPass(true); return; }
    if (WaterReflectionActive) { BeginWaterPass(true); return; }
    ResumeScenePass();
}

void VulkanRenderDevice::ResumeScenePass()
{
    auto drawcommands = Commands->GetDrawCommands();

    VkAccessFlags srcColorAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	VkAccessFlags dstColorAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	VkAccessFlags srcDepthAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	VkAccessFlags dstDepthAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

	PipelineBarrier()
		.AddImage(Textures->Scene->ColorBuffer.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcColorAccess, dstColorAccess)
		.AddImage(Textures->Scene->HitBuffer.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcColorAccess, dstColorAccess)
		.AddImage(Textures->Scene->DepthBuffer.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, srcDepthAccess, dstDepthAccess, VK_IMAGE_ASPECT_DEPTH_BIT)
		.Execute(drawcommands, srcStages, dstStages);

	BeginSceneSegment(true);
}

void VulkanRenderDevice::Unlock(bool Blit)
{
    if (ScenePassDeferred) StartScenePass();
	DrawBatch(Commands->GetDrawCommands());
	EndSceneSegment(true);
	if (AmbientOcclusionMode() != 0)
	{
		PipelineBarrier().AddImage(Textures->Scene->DepthBuffer.get(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
			.Execute(Commands->GetDrawCommands(), VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	Commands->SetProfilePass(CommandBufferManager::GpuPass::Resolve);
	BlitSceneToPostprocess();
	if (Bloom || VisualEnhanced)
	{
		Commands->SetProfilePass(CommandBufferManager::GpuPass::Bloom);
		RunBloomPass();
	}
	Commands->SetProfilePass(CommandBufferManager::GpuPass::Other);

	int windowWidth = Viewport->GetNativePixelWidth();
	int windowHeight = Viewport->GetNativePixelHeight();

	SubmitFrame(Blit ? true : false, windowWidth, windowHeight, Viewport->IsFullscreen());
    if (Blit) {
        auto dimension=[](int value) { return uint64_t(clamp(value,0,32767)); };
        VisualComparison::ScalingSnapshot.store(dimension(Textures->Scene->Width) |
            (dimension(Textures->Scene->Height)<<15) | (dimension(windowWidth)<<30) |
            (dimension(windowHeight)<<45) | (uint64_t(UpscaleSGSR && ResolutionScale<1.0f)<<60));
    }

	Batch.Pipeline = nullptr;

	if (Samplers->LODBias != LODBias || Samplers->RequestedAnisotropy != TextureAnisotropy)
	{
		Commands->WaitForPending();
		DescriptorSets->ClearCache();
		Textures->ClearAllBindlessIndexes();
		Samplers->CreateSceneSamplers();
	}

	if (HitData)
	{
		// Look for the last hit
		int width = HitWidth;
		int height = HitHeight;
		int hit = 0;
		const int32_t* data = (const int32_t*)Textures->Scene->StagingHitBuffer->Map(0, width * height * sizeof(int32_t));
		if (data)
		{
			for (int y = 0; y < height; y++)
			{
				const int* line = data + y * width;
				for (int x = 0; x < width; x++)
				{
					hit = std::max(hit, line[x]);
				}
			}
			Textures->Scene->StagingHitBuffer->Unmap();
		}
		hit--;

		hit = std::max(hit, ForceHitIndex);

		if (hit >= 0 && hit < (int)HitQueries.size())
		{
			const HitQuery& query = HitQueries[hit];
			memcpy(HitData, HitBuffer.data() + query.Start, query.Count);
			*HitSize = query.Count;
		}
		else
		{
			*HitSize = 0;
		}
	}

	HitQueryStack.clear();
	HitQueries.clear();
	HitBuffer.clear();
	HitData = nullptr;
	HitSize = nullptr;

	IsLocked = false;
}

bool VulkanRenderDevice::SupportsTextureFormat(TextureFormat Format)
{
	return Uploads->SupportsTextureFormat(Format);
}

void VulkanRenderDevice::UpdateTextureRect(TextureInfo& Info, int U, int V, int UL, int VL)
{
	Textures->UpdateTextureRect(&Info, U, V, UL, VL);
}

void VulkanRenderDevice::DrawBatch(VulkanCommandBuffer* cmdbuffer)
{
	size_t icount = SceneIndexPos - Batch.SceneIndexStart;
	if (icount > 0)
	{
        if (ScenePassDeferred && !DrawingShadow && !WaterReflectionActive) StartScenePass();
		if (RecordedScene) cmdbuffer = RecordedScene.get();
		if (viewportdesc.minDepth != Batch.Pipeline->MinDepth || viewportdesc.maxDepth != Batch.Pipeline->MaxDepth)
		{
			viewportdesc.minDepth = Batch.Pipeline->MinDepth;
			viewportdesc.maxDepth = Batch.Pipeline->MaxDepth;
			cmdbuffer->setViewport(0, 1, &viewportdesc);
		}

		auto layout = RenderPasses->Scene.BindlessPipelineLayout.get();
		cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, Batch.Pipeline->Pipeline.get());
		cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, DescriptorSets->GetBindlessSet());
		cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, WaterReflectionActive?Buffers->ReflectionLightSet.get():Buffers->MeshLightSet.get());
		const vec3 waveCamera=Buffers->ShadowLighting->CameraPosition.xyz();
        pushconstants.padding1=std::bit_cast<uint32_t>(waveCamera.x);
        pushconstants.padding2=std::bit_cast<uint32_t>(waveCamera.y);
        pushconstants.padding3=std::bit_cast<uint32_t>(UsesCrestWater()?1.f:0.f);
        cmdbuffer->pushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ScenePushConstants), &pushconstants);
		cmdbuffer->setBlendConstants(Batch.BlendConstants);
		cmdbuffer->drawIndexed((uint32_t)icount, 1, (uint32_t)Batch.SceneIndexStart, 0, 0);
		Batch.SceneIndexStart = SceneIndexPos;
		Stats.DrawCalls++;
	}
}

void VulkanRenderDevice::DrawComplexSurface(SceneNode* Frame, SurfaceInfo& Surface, SurfaceFacet& Facet)
{
	if (Facet.VertexCount < 3)
		return;

	uint32_t PolyFlags = ApplyPrecedenceRules(Surface.PolyFlags);
    if (DrawingShadow)
    {
        if (!Surface.Texture || (PolyFlags & (PF_Invisible | PF_FakeBackdrop | PF_Portal | PF_Translucent | PF_Modulated))) return;
        std::vector<GouraudVertex> vertices(Facet.VertexCount);
        for (int i=0; i<Facet.VertexCount; ++i)
        {
            vertices[i].Point = Facet.Vertices[i];
            vec3 delta = Facet.Vertices[i] - Facet.MapCoords.Origin;
            vertices[i].UV = vec2(dot(Facet.MapCoords.XAxis,delta)-Surface.Texture->Pan.x, dot(Facet.MapCoords.YAxis,delta)-Surface.Texture->Pan.y);
        }
        DrawShadowPolygon(*Surface.Texture, vertices.data(), Facet.VertexCount, PolyFlags);
        return;
    }

	if (WaterReflectionActive && Facet.Water) return;
    bool water=Facet.Water && (WorldFeatures&2048) && Buffers->ShadowLighting->WaterParameters.z>0.5f &&
        Facet.VertexCount>0 && (UsesModernWater() || std::abs(Facet.Vertices[0].z-Buffers->ShadowLighting->WaterParameters.y)<1.0f);
    bool opaqueWater=water && !(Surface.PolyFlags&(PF_Translucent|PF_Modulated));
    if(water && UsesCrestWater())PolyFlags=(PolyFlags&~(PF_Translucent|PF_Modulated|PF_Masked|PF_Highlighted))|PF_Occlude;
    else if(water && !opaqueWater) PolyFlags=(PolyFlags & ~(PF_Translucent|PF_Modulated|PF_Masked|PF_Occlude))|PF_Highlighted;
    CachedTexture* tex = Textures->GetTexture(Surface.Texture, !!(PolyFlags & PF_Masked));
	CachedTexture* lightmap = Textures->GetTexture(Surface.LightMap, false);
	CachedTexture* macrotex = Textures->GetTexture(Surface.MacroTexture, false);
	CachedTexture* detailtex = Textures->GetTexture(Surface.DetailTexture, false);
	CachedTexture* fogmap = (Surface.FogMap && Surface.FogMap->NumMips > 0 && !Surface.FogMap->Mips[0].Data.empty()) ? Textures->GetTexture(Surface.FogMap, false) : nullptr;

	if (Surface.DetailTexture && Surface.FogMap) detailtex = nullptr;

	float UDot = dot(Facet.MapCoords.XAxis, Facet.MapCoords.Origin);
	float VDot = dot(Facet.MapCoords.YAxis, Facet.MapCoords.Origin);

	float UPan = tex ? UDot + Surface.Texture->Pan.x : 0.0f;
	float VPan = tex ? VDot + Surface.Texture->Pan.y : 0.0f;
	float UMult = tex ? GetUMult(*Surface.Texture) : 0.0f;
	float VMult = tex ? GetVMult(*Surface.Texture) : 0.0f;
	float LMUPan = lightmap ? UDot + Surface.LightMap->Pan.x - 0.5f * Surface.LightMap->UScale : 0.0f;
	float LMVPan = lightmap ? VDot + Surface.LightMap->Pan.y - 0.5f * Surface.LightMap->VScale : 0.0f;
	float LMUMult = lightmap ? GetUMult(*Surface.LightMap) : 0.0f;
	float LMVMult = lightmap ? GetVMult(*Surface.LightMap) : 0.0f;
	float MacroUPan = macrotex ? UDot + Surface.MacroTexture->Pan.x : 0.0f;
	float MacroVPan = macrotex ? VDot + Surface.MacroTexture->Pan.y : 0.0f;
	float MacroUMult = macrotex ? GetUMult(*Surface.MacroTexture) : 0.0f;
	float MacroVMult = macrotex ? GetVMult(*Surface.MacroTexture) : 0.0f;
	float DetailUPan = UPan;
	float DetailVPan = VPan;
	float DetailUMult = detailtex ? GetUMult(*Surface.DetailTexture) : 0.0f;
	float DetailVMult = detailtex ? GetVMult(*Surface.DetailTexture) : 0.0f;

	uint32_t flags = water?(524288u|(opaqueWater?131072u:0u)):0u;
	if (lightmap) flags |= 1;
	if (macrotex) flags |= 2;
	if (detailtex && !fogmap) flags |= 4;
	if (fogmap) flags |= 8;

	if (LightMode == 1) flags |= 64;
    CachedTexture* relief = nullptr;
    if ((WorldFeatures & 6) && !macrotex && (PolyFlags & (PF_Unlit | PF_Translucent | PF_Modulated | PF_Masked | PF_FakeBackdrop | PF_AutoUPan | PF_AutoVPan | PF_SmallWavy | PF_BigWavy)) == 0)
        relief = Textures->GetRelief(Surface.Texture);
    if (relief) {
        flags |= 512;
        if(WorldFeatures & 2) flags |= 1024;
        if(WorldFeatures & 4) flags |= 2048;
        // Legacy enabled flags retain the old Medium strength.
        int detail=(WorldFeatures>>4)&3, depth=(WorldFeatures>>6)&3;
        flags |= (detail ? detail : 2)<<12;
        flags |= (depth ? depth : 2)<<14;
    }

	if (fogmap) // if Surface.FogMap exists, use instead of detail texture
	{
		detailtex = fogmap;
		DetailUPan = UDot + Surface.FogMap->Pan.x - 0.5f * Surface.FogMap->UScale;
		DetailVPan = VDot + Surface.FogMap->Pan.y - 0.5f * Surface.FogMap->VScale;
		DetailUMult = GetUMult(*Surface.FogMap);
		DetailVMult = GetVMult(*Surface.FogMap);
	}

	SetPipeline(RenderPasses->GetPipeline(PolyFlags, water));

    SurfaceLight* surfaceLight = lightmap && (WorldFeatures & 256) ? Surface.LightMap->PixelLighting : nullptr;
    if (surfaceLight && !surfaceLight->Count && !(WorldFeatures&512)) surfaceLight = nullptr;
    if (surfaceLight && Buffers->MeshLightCount == BufferManager::MaxMeshLights) FlushDrawBatchAndWait();
	ivec4 textureBinds = GetTextureIndexes(PolyFlags, tex, lightmap, relief ? relief : macrotex, detailtex);
    int materialMap=0;
    if(surfaceLight && surfaceLight->Count && (WorldFeatures&1024)) materialMap=DescriptorSets->GetTextureArrayIndex(0,Textures->GetMaterialMap(Surface.Texture));
    int responseMap=materialMap?DescriptorSets->GetTextureArrayIndex(0,Textures->GetMaterialResponseMap(Surface.Texture)):0;
    int shadowIndexes[2] = {0, 0};
    if (surfaceLight)
    {
        for (int i = 0; i < (surfaceLight->Count + 3) / 4; i++)
        {
            TextureInfo shadow = *Surface.LightMap;
            shadow.CacheID = (shadow.CacheID & ~uint64_t(255)) | (6 + i);
            shadow.PixelLighting = nullptr;
            shadow.Mips = &surfaceLight->Shadows[i];
            shadow.bRealtimeChanged = surfaceLight->ShadowChanged[i];
            auto* texture = Textures->GetTexture(&shadow, false);
            shadowIndexes[i] = DescriptorSets->GetTextureArrayIndex(0, texture);
            surfaceLight->ShadowChanged[i] = false;
        }
    }
	vec4 color(1.0f);
    if (relief)
    {
        vec3 light=Facet.ReliefLightDirection;
        float len=length(light);
        light = len > 0.001f ? light / len : vec3(0.0f);
        vec3 normal = normalize(cross(Facet.MapCoords.XAxis, Facet.MapCoords.YAxis));
        vec3 tangent = normalize(cross(Facet.MapCoords.YAxis, normal));
        vec3 bitangent = normalize(cross(normal, Facet.MapCoords.XAxis));
        mat4 toView = Frame->WorldToView * Frame->ObjectToWorld;
        vec4 viewNormal = toView * vec4(normal, 0.0f);
        vec4 viewPoint = toView * vec4(Facet.Vertices[0], 1.0f);
        if (dot(vec3(viewNormal.x,viewNormal.y,viewNormal.z), vec3(viewPoint.x,viewPoint.y,viewPoint.z)) > 0.0f) normal = -normal;
        color=vec4(dot(light,tangent),dot(light,bitangent),dot(light,normal),1.0f);
    }

    if(water && Crest && Crest->Frames==120 && commandline->HasArg("","--trace-rune-water")) {
        std::string line="RUNE_CREST_SURFACE texture="+(Surface.Texture && Surface.Texture->Texture?Surface.Texture->Texture->Name.ToString():std::string("none"));
        for(int i=0;i<Facet.VertexCount;++i)line+=" p="+std::to_string(Facet.Vertices[i].x)+","+std::to_string(Facet.Vertices[i].y)+","+std::to_string(Facet.Vertices[i].z)+","+std::to_string(Facet.WaterDepth?Facet.WaterDepth[i]:-1.f);
        LogMessage(line);
    }
        const bool crestSurface=water && UsesCrestWater();
        const vec3 crestViewCamera=CurrentLightingCamera;
        float crestMainView=0;
        if(crestSurface) {
            // The shadow UBO is GPU-mapped memory: read it once per surface,
            // never inside the streamed vertex loop.
            vec3 delta=crestViewCamera-Buffers->ShadowLighting->CameraPosition.xyz();
            crestMainView=dot(delta,delta)<.01f?1.f:0.f;
        }
    const bool displaceCrest = crestSurface && crestMainView > .5f && !StableCrestSurface &&
        crestViewCamera.z > Facet.Vertices[0].z - 24.f;
    if(crestSurface && Crest->Frames==120 && commandline->HasArg("","--trace-rune-water"))
        LogMessage("RUNE_CREST_CARRIER main="+std::to_string(crestMainView)+" plane="+std::to_string(Facet.Vertices[0].z)+" camera_z="+std::to_string(crestViewCamera.z)+" displacement="+std::to_string(displaceCrest));
    // Rotate the miniature skybox ray into the main world's frame. Shading
    // projects that ray onto the real water height; horizon geometry stays flat.
    const mat3 skyWaterToMain=crestSurface && crestMainView<.5f ? WaterViewToWorld*mat3(Frame->WorldToView) : mat3(mat4::identity());
    const vec3 mainWaterCamera=crestSurface?Buffers->ShadowLighting->CameraPosition.xyz():vec3(0);
    const CrestWater::Mesh* crestMesh = displaceCrest ? &Crest->Tessellate(Facet, CurrentLightingCamera) : nullptr;
    auto pts=crestMesh?crestMesh->Points.data():Facet.Vertices;
    uint32_t vcount=crestMesh?uint32_t(crestMesh->Points.size()):Facet.VertexCount;
    if(vcount<3)return;
    uint32_t icount=crestMesh?vcount:(vcount-2)*3;
	auto alloc = ReserveVertices(vcount, icount);
	if (alloc.vptr)
	{
        if (surfaceLight)
        {
            MeshLightPacket packet{};
            packet.Material = GetMaterialResponse(Surface.Texture);
            packet.Material.w=(WorldFeatures&65536)?1.0f:0.0f;
        if(packet.Material.w>0)packet.Material.y=std::sqrt(std::sqrt(2.0f/(packet.Material.y+2.0f)));
            packet.MaterialMaps=ivec4(materialMap,materialMap?1:0,responseMap,responseMap?1:0);
            packet.ViewCamera=vec4(CurrentLightingCamera,1);
            packet.AmbientScale = vec4(0, 0, 0, surfaceLight->Time);
            packet.Meta = ivec4(surfaceLight->Count, shadowIndexes[0], shadowIndexes[1], 0);
            packet.SurfaceOrigin = vec4(surfaceLight->Origin, surfaceLight->Normal.x);
            packet.SurfaceU = vec4(surfaceLight->U, surfaceLight->Normal.y);
            packet.SurfaceV = vec4(surfaceLight->V, surfaceLight->Normal.z);
            // BSP geometry has a constant texture basis. Deriving it once also
            // keeps normal-map orientation independent of parallax UV offsets.
            vec3 normal=surfaceLight->Normal;
            if(std::abs(UMult)>1e-12f && std::abs(VMult)>1e-12f) {
                vec3 tangent=surfaceLight->U*(LMUMult/UMult);
                vec3 rawB=surfaceLight->V*(LMVMult/VMult);
                tangent-=normal*dot(normal,tangent);
                float len2=dot(tangent,tangent);
                if(std::isfinite(len2) && len2>1e-12f) {
                    tangent/=std::sqrt(len2);
                    vec3 bitangent=cross(normal,tangent);
                    if(dot(bitangent,rawB)<0)bitangent=-bitangent;
                    packet.SurfaceTangent=vec4(tangent,0);
                    packet.SurfaceBitangent=vec4(bitangent,1);
                }
            }
            int contactMask=0;
            // Bound the shader's reconstructed receiver positions. This also
            // preserves cached mover lightmaps whose bake transform may differ
            // from the brush's current ObjectToWorld matrix.
            auto worldPoint=[&](int vertex){
                float u=(dot(Facet.MapCoords.XAxis,Facet.Vertices[vertex])-LMUPan)*LMUMult;
                float v=(dot(Facet.MapCoords.YAxis,Facet.Vertices[vertex])-LMVPan)*LMVMult;
                return surfaceLight->Origin+surfaceLight->U*u+surfaceLight->V*v;
            };
            vec3 boundsMin=worldPoint(0),boundsMax=boundsMin;
            for(int v=1;v<Facet.VertexCount;++v) {
                vec3 point=worldPoint(v);
                for(int axis=0;axis<3;++axis) {
                    boundsMin[axis]=std::min(boundsMin[axis],point[axis]);
                    boundsMax[axis]=std::max(boundsMax[axis],point[axis]);
                }
            }
            for(int c=0;c<4;++c) {
                const auto plane=Buffers->ShadowLighting->ContactPlanes[c];
                const auto center=Buffers->ShadowLighting->ContactCenters[c];
                if(plane.w<=0 || std::abs(dot(normal,plane.xyz()))<0.849f)continue;
                float low=1e30f,high=-1e30f;
                for(int v=0;v<Facet.VertexCount;++v) {
                    float height=dot(worldPoint(v)-center.xyz(),plane.xyz());
                    low=std::min(low,height);high=std::max(high,height);
                }
                if(low>=2.01f || high<=-2.01f)continue;
                vec3 separation(0);
                vec3 centerPosition=center.xyz();
                for(int axis=0;axis<3;++axis)
                    separation[axis]=std::max(std::max(boundsMin[axis]-centerPosition[axis],centerPosition[axis]-boundsMax[axis]),0.0f);
                if(center.w>0 && dot(separation,separation)>1.0f/center.w+4.1f)continue;
                contactMask|=1<<c;
            }
            packet.SurfaceTangent.w=float(contactMask);
            if((WorldFeatures&(512|131072))==(512|131072) && engine && engine->Level) {
                vec3 center=(boundsMin+boundsMax)*.5f+normal*4.0f;
                auto environment=engine->Level->Light.SampleEnvironment(center);
                std::copy(environment.Lobes.begin(),environment.Lobes.end(),packet.Environment);
            }
            for (int i = 0; i < surfaceLight->Count; i++)
            {
                const auto& light = surfaceLight->Lights[i];
                packet.Lights[i].PositionRadius = vec4(light.Position, light.InvRadiusSquared);
                packet.Lights[i].Color = vec4(light.Color, light.WaveSpeed);
                for(int slot=0;slot<RuneShadows::Capacity;++slot) {
                    const auto& shadow=Buffers->ShadowLighting->Shadows[slot];
                    vec3 delta=light.Position-shadow.LightPosition.xyz();
                    if(shadow.LightPosition.w>0 && dot(delta,delta)<0.25f &&
                        (!CullShadowReceivers || ShadowReceiverMayIntersect(shadow.WorldToShadow,boundsMin,boundsMax)))
                        { packet.Meta.w|=std::bit_cast<int32_t>(RuneShadows::Slots::Encode(i,slot)); break; }
                }
            }
            int index = Buffers->MeshLightCount++;
            Buffers->MeshLights[index] = packet;
            // Four texture lanes are already used by world materials. Upper
            // flag bits carry the bounded 12-bit packet index instead.
            flags |= 262144u | (uint32_t(index) << 20);
        }
		SceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;


		for (uint32_t i = 0; i < vcount; i++)
		{
			vec3 point = pts[i];
			float u = dot(Facet.MapCoords.XAxis, point);
			float v = dot(Facet.MapCoords.YAxis, point);

			vptr->Flags = flags;
			vptr->Position.x = point.x;
			vptr->Position.y = point.y;
			vptr->Position.z = point.z;
			vptr->TexCoord.s = (u - UPan) * UMult;
			vptr->TexCoord.t = (v - VPan) * VMult;
			vptr->TexCoord2.s = (u - LMUPan) * LMUMult;
			vptr->TexCoord2.t = (v - LMVPan) * LMVMult;
			vptr->TexCoord3.s = (u - MacroUPan) * MacroUMult;
			vptr->TexCoord3.t = (v - MacroVPan) * MacroVMult;
			vptr->TexCoord4.s = (u - DetailUPan) * DetailUMult;
			vptr->TexCoord4.t = (v - DetailVPan) * DetailVMult;
            if(crestSurface) {
                vptr->Normal=crestMainView>.5f?crestViewCamera:skyWaterToMain*(point-crestViewCamera);
                vptr->TexCoord3.x=crestMainView;
                vptr->TexCoord3.y = crestMainView > .5f ? float(displaceCrest) : mainWaterCamera.z;
            }
			vptr->Color = color;
            if(water && UsesModernWater()) vptr->Color.w=crestMesh ? crestMesh->Depth[i] : Facet.WaterDepth ? Facet.WaterDepth[i] : 512.0f;
			vptr->TextureBinds = textureBinds;
			vptr++;
		}

        if(crestMesh) { for(uint32_t i=0;i<vcount;++i)*(iptr++)=vpos+i; }
        else for (uint32_t i = vpos + 2; i < vpos + vcount; i++)
		{
			*(iptr++) = vpos;
			*(iptr++) = i - 1;
			*(iptr++) = i;
		}

		UseVertices(vcount, icount);
	}

	Stats.ComplexSurfaces++;
}

void VulkanRenderDevice::DrawGouraudPolygon(SceneNode* Frame, TextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags)
{
	if (NumPts < 3) return; // This can apparently happen!!
    if (DrawingShadow) { DrawShadowPolygon(Info, Pts, NumPts, PolyFlags); return; }

	PolyFlags = ApplyPrecedenceRules(PolyFlags);
	bool alphaFade = Pts[0].Opacity < 1.0f;
	bool cameraFade = alphaFade && Pts[0].CameraFade;
	if (cameraFade)
		PolyFlags = (PolyFlags & ~(PF_Highlighted | PF_Translucent | PF_Modulated)) | PF_Occlude;
	else if (alphaFade)
		PolyFlags = (PolyFlags & ~(PF_Occlude | PF_Translucent | PF_Modulated)) | PF_Highlighted;

	SetPipeline(RenderPasses->GetPipeline(PolyFlags, false, cameraFade));

	CachedTexture* tex = Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));
	ivec4 textureBinds = GetTextureIndexes(PolyFlags, tex);

	float UMult = GetUMult(Info);
	float VMult = GetVMult(Info);
	int flags = (PolyFlags & (PF_RenderFog | PF_Translucent | PF_Modulated)) == PF_RenderFog ? 16 : 0;
	if (cameraFade) flags |= 256;
	else if (alphaFade) flags |= 128;

	if ((PolyFlags & (PF_Translucent | PF_Modulated)) == 0 && LightMode == 2) flags |= 32;

	bool pixelLight = (WorldFeatures & 256) && Pts[0].Lighting && !(PolyFlags & (PF_Unlit | PF_Modulated));
	if (pixelLight && Buffers->MeshLightCount == BufferManager::MaxMeshLights) FlushDrawBatchAndWait();
	int materialMap=0;
    if(pixelLight && (WorldFeatures&1024)) materialMap=DescriptorSets->GetTextureArrayIndex(0,Textures->GetMaterialMap(&Info));
    int responseMap=materialMap?DescriptorSets->GetTextureArrayIndex(0,Textures->GetMaterialResponseMap(&Info)):0;
    auto alloc = ReserveVertices(NumPts, (NumPts - 2) * 3);
	if (pixelLight && alloc.vptr)
	{
		const auto& src = *Pts[0].Lighting;
		MeshLightPacket packet{};
		packet.Material = GetMaterialResponse(&Info);
        packet.Material.w=(WorldFeatures&65536)?1.0f:0.0f;
        if(packet.Material.w>0)packet.Material.y=std::sqrt(std::sqrt(2.0f/(packet.Material.y+2.0f)));
        if((WorldFeatures&(512|131072))==(512|131072))
            std::copy(src.Environment.Lobes.begin(),src.Environment.Lobes.end(),packet.Environment);
        packet.MaterialMaps=ivec4(materialMap,materialMap?1:0,responseMap,responseMap?1:0);
        packet.ViewCamera=vec4(CurrentLightingCamera,1);
        packet.AmbientScale = vec4(src.AmbientColor, src.ScaleGlow);
		packet.Meta = ivec4(std::clamp(src.NumLights, 0, 8), 0, 0, 0);
		for (int i = 0; i < packet.Meta.x; i++)
		{
			packet.Lights[i].PositionRadius = vec4(src.Lights[i].Location, src.Lights[i].InvRadiusSquared);
			packet.Lights[i].Color = vec4(src.Lights[i].Color, 0.0f);
            for(int slot=0;slot<RuneShadows::Capacity;++slot) {
                const auto& shadow=Buffers->ShadowLighting->Shadows[slot];
                vec3 delta=src.Lights[i].Location-shadow.LightPosition.xyz();
                if(shadow.LightPosition.w>0 && dot(delta,delta)<0.25f) { packet.Meta.w|=std::bit_cast<int32_t>(RuneShadows::Slots::Encode(i,slot)); break; }
            }
		}
		int index = Buffers->MeshLightCount - 1;
		if (index < 0 || std::memcmp(&packet, &Buffers->MeshLights[index], sizeof(packet)) != 0)
		{
			index = Buffers->MeshLightCount++;
			Buffers->MeshLights[index] = packet;
		}
		textureBinds.y = index;
		flags |= 65536;
		if (PolyFlags & PF_TwoSided) flags |= 131072;
	}

	if (alloc.vptr)
	{
		SceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		if (PolyFlags & PF_Modulated)
		{
			SceneVertex* vertex = vptr;
			for (int i = 0; i < NumPts; i++)
			{
				const GouraudVertex* P = &Pts[i];
				vertex->Flags = flags;
				vertex->Position.x = P->Point.x;
				vertex->Position.y = P->Point.y;
				vertex->Position.z = P->Point.z;
				vertex->TexCoord.s = P->UV.x * UMult;
				vertex->TexCoord.t = P->UV.y * VMult;
				vertex->TexCoord2.s = P->Fog.x;
				vertex->TexCoord2.t = P->Fog.y;
				vertex->TexCoord3.s = P->Fog.z;
				vertex->TexCoord3.t = P->Fog.w;
				vertex->TexCoord4.s = P->Opacity;
				vertex->TexCoord4.t = 0.0f;
				vertex->Color.r = 1.0f;
				vertex->Color.g = 1.0f;
				vertex->Color.b = 1.0f;
				vertex->Color.a = 1.0f;
				vertex->TextureBinds = textureBinds;
				vertex->Normal = P->Normal;
				vertex++;
			}
		}
		else
		{
			SceneVertex* vertex = vptr;
			for (int i = 0; i < NumPts; i++)
			{
				const GouraudVertex* P = &Pts[i];
				vertex->Flags = flags;
				vertex->Position.x = P->Point.x;
				vertex->Position.y = P->Point.y;
				vertex->Position.z = P->Point.z;
				vertex->TexCoord.s = P->UV.x * UMult;
				vertex->TexCoord.t = P->UV.y * VMult;
				vertex->TexCoord2.s = P->Fog.x;
				vertex->TexCoord2.t = P->Fog.y;
				vertex->TexCoord3.s = P->Fog.z;
				vertex->TexCoord3.t = P->Fog.w;
				vertex->TexCoord4.s = P->Opacity;
				vertex->TexCoord4.t = 0.0f;
				vertex->Color.r = P->Light.x;
				vertex->Color.g = P->Light.y;
				vertex->Color.b = P->Light.z;
				vertex->Color.a = 1.0f;
				vertex->TextureBinds = textureBinds;
				vertex->Normal = P->Normal;
				vertex++;
			}
		}

		uint32_t vstart = vpos;
		uint32_t vcount = NumPts;
		for (uint32_t i = vstart + 2; i < vstart + vcount; i++)
		{
			*(iptr++) = vstart;
			*(iptr++) = i - 1;
			*(iptr++) = i;
		}

		UseVertices(NumPts, (NumPts - 2) * 3);
	}

	Stats.GouraudPolygons++;
}

void VulkanRenderDevice::DrawTile(SceneNode* Frame, TextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags)
{
    // Rune canvas style 5 supplies uniform opacity. Keep legacy tile callers
    // (whose DrawColor alpha is often zero) on their original opaque behavior.
    const bool canvasAlpha = PolyFlags != PF_SubpixelFont && (PolyFlags & PF_CanvasAlpha) != 0;
    if (canvasAlpha)
    {
        PolyFlags = (PolyFlags & ~PF_CanvasAlpha) | PF_Highlighted;
        float opacity = std::isfinite(Color.w) ? clamp(Color.w, 0.0f, 1.0f) : 1.0f;
        if (opacity <= 0.0f) return;
        Color = vec4(Color.xyz() * opacity, opacity); // Premultiplied blend pipeline.
    }
	PolyFlags = ApplyPrecedenceRules(PolyFlags);

	if (PolyFlags == PF_SubpixelFont)
	{
		DrawBatch(Commands->GetDrawCommands());
		Batch.BlendConstants[0] = Color.x;
		Batch.BlendConstants[1] = Color.y;
		Batch.BlendConstants[2] = Color.z;
		Batch.BlendConstants[3] = Color.w;
		Color = vec4(1.0f);
	}

	CachedTexture* tex = Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));
	float UMult = tex ? GetUMult(Info) : 0.0f;
	float VMult = tex ? GetVMult(Info) : 0.0f;
	float u0 = U * UMult;
	float v0 = V * VMult;
	float u1 = (U + UL) * UMult;
	float v1 = (V + VL) * VMult;
	bool clamp = (u0 >= 0.0f && u1 <= 1.00001f && v0 >= 0.0f && v1 <= 1.00001f);

	SetPipeline(RenderPasses->GetPipeline(PolyFlags));
	ivec4 textureBinds = GetTextureIndexes(PolyFlags, tex, clamp);

	float r, g, b, a;
	if (PolyFlags & PF_Modulated)
	{
		r = 1.0f;
		g = 1.0f;
		b = 1.0f;
	}
	else
	{
		r = Color.x;
		g = Color.y;
		b = Color.z;
	}
	a = canvasAlpha ? Color.w : 1.0f;

	if (Textures->Scene->Multisample > 1)
	{
		XL = std::floor(X + XL + 0.5f);
		YL = std::floor(Y + YL + 0.5f);
		X = std::floor(X + 0.5f);
		Y = std::floor(Y + 0.5f);
		XL = XL - X;
		YL = YL - Y;
	}

	auto alloc = ReserveVertices(4, 6);
	if (alloc.vptr)
	{
		SceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		vptr[0] = { 0, vec3(RFX2 * Z * (X - Frame->FX2),      RFY2 * Z * (Y - Frame->FY2),      Z), vec2(u0, v0), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec4(r, g, b, a), textureBinds };
		vptr[1] = { 0, vec3(RFX2 * Z * (X + XL - Frame->FX2), RFY2 * Z * (Y - Frame->FY2),      Z), vec2(u1, v0), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec4(r, g, b, a), textureBinds };
		vptr[2] = { 0, vec3(RFX2 * Z * (X + XL - Frame->FX2), RFY2 * Z * (Y + YL - Frame->FY2), Z), vec2(u1, v1), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec4(r, g, b, a), textureBinds };
		vptr[3] = { 0, vec3(RFX2 * Z * (X - Frame->FX2),      RFY2 * Z * (Y + YL - Frame->FY2), Z), vec2(u0, v1), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), vec4(r, g, b, a), textureBinds };

		iptr[0] = vpos;
		iptr[1] = vpos + 1;
		iptr[2] = vpos + 2;
		iptr[3] = vpos;
		iptr[4] = vpos + 2;
		iptr[5] = vpos + 3;

		UseVertices(4, 6);
	}

	Stats.Tiles++;
}

vec4 VulkanRenderDevice::ApplyInverseGamma(vec4 color)
{
	if (IsOrtho)
		return color;
	float brightness = clamp(Brightness * 2.0f, 0.05f, 2.99f);
	float gammaRed = std::max(brightness + GammaOffset + GammaOffsetRed, 0.001f);
	float gammaGreen = std::max(brightness + GammaOffset + GammaOffsetGreen, 0.001f);
	float gammaBlue = std::max(brightness + GammaOffset + GammaOffsetBlue, 0.001f);
	return vec4(pow(color.r, gammaRed), pow(color.g, gammaGreen), pow(color.b, gammaBlue), color.a);
}

void VulkanRenderDevice::Draw3DLine(SceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	if (IsOrtho)
	{
		P1.x = (P1.x) / Frame->Zoom + Frame->FX2;
		P1.y = (P1.y) / Frame->Zoom + Frame->FY2;
		P1.z = 1;
		P2.x = (P2.x) / Frame->Zoom + Frame->FX2;
		P2.y = (P2.y) / Frame->Zoom + Frame->FY2;
		P2.z = 1;

		if (std::abs(P2.x - P1.x) + std::abs(P2.y - P1.y) >= 0.2f)
		{
			Draw2DLine(Frame, Color, LineFlags, P1, P2);
		}
		else if (IsOrthoLowDetail)
		{
			Draw2DPoint(Frame, Color, LINE_None, P1.x - 1, P1.y - 1, P1.x + 1, P1.y + 1, P1.z);
		}
	}
	else
	{
		bool occlude = !!(LineFlags & LINE_DepthCued);
		SetPipeline(RenderPasses->GetLinePipeline(occlude));
		ivec4 textureBinds = GetTextureIndexes(PF_Highlighted, nullptr);
		vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

		auto alloc = ReserveVertices(2, 2);
		if (alloc.vptr)
		{
			SceneVertex* vptr = alloc.vptr;
			uint32_t* iptr = alloc.iptr;
			uint32_t vpos = alloc.vpos;

			vptr[0] = { 0, vec3(P1.x, P1.y, P1.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };
			vptr[1] = { 0, vec3(P2.x, P2.y, P2.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };

			iptr[0] = vpos;
			iptr[1] = vpos + 1;

			UseVertices(2, 2);
		}
	}
}

void VulkanRenderDevice::Draw2DLine(SceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	bool occlude = !!(LineFlags & LINE_DepthCued);
	SetPipeline(RenderPasses->GetLinePipeline(occlude));
	ivec4 textureBinds = GetTextureIndexes(PF_Highlighted, nullptr);
	vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

	auto alloc = ReserveVertices(2, 2);
	if (alloc.vptr)
	{
		SceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		vptr[0] = { 0, vec3(RFX2 * P1.z * (P1.x - Frame->FX2), RFY2 * P1.z * (P1.y - Frame->FY2), P1.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };
		vptr[1] = { 0, vec3(RFX2 * P2.z * (P2.x - Frame->FX2), RFY2 * P2.z * (P2.y - Frame->FY2), P2.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };

		iptr[0] = vpos;
		iptr[1] = vpos + 1;

		UseVertices(2, 2);
	}
}

void VulkanRenderDevice::Draw2DPoint(SceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z)
{
	bool occlude = !!(LineFlags & LINE_DepthCued);
	SetPipeline(RenderPasses->GetPointPipeline(occlude));
	ivec4 textureBinds = GetTextureIndexes(PF_Highlighted, nullptr);
	vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

	auto alloc = ReserveVertices(4, 6);
	if (alloc.vptr)
	{
		SceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		vptr[0] = { 0, vec3(RFX2 * Z * (X1 - Frame->FX2 - 0.5f), RFY2 * Z * (Y1 - Frame->FY2 - 0.5f), Z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };
		vptr[1] = { 0, vec3(RFX2 * Z * (X2 - Frame->FX2 + 0.5f), RFY2 * Z * (Y1 - Frame->FY2 - 0.5f), Z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };
		vptr[2] = { 0, vec3(RFX2 * Z * (X2 - Frame->FX2 + 0.5f), RFY2 * Z * (Y2 - Frame->FY2 + 0.5f), Z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };
		vptr[3] = { 0, vec3(RFX2 * Z * (X1 - Frame->FX2 - 0.5f), RFY2 * Z * (Y2 - Frame->FY2 + 0.5f), Z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, textureBinds };

		iptr[0] = vpos;
		iptr[1] = vpos + 1;
		iptr[2] = vpos + 2;
		iptr[3] = vpos;
		iptr[4] = vpos + 2;
		iptr[5] = vpos + 3;

		UseVertices(4, 6);
	}
}

void VulkanRenderDevice::ClearZ()
{
    if (ScenePassDeferred && !WaterReflectionActive) StartScenePass();
	DrawBatch(Commands->GetDrawCommands());

	VkClearAttachment attachment = {};
	VkClearRect rect = {};
	attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	attachment.clearValue.depthStencil.depth = 1.0f;
	rect.layerCount = 1;
	rect.rect.extent.width = WaterReflectionActive?WaterSize:Textures->Scene->Width;
	rect.rect.extent.height = WaterReflectionActive?WaterSize:Textures->Scene->Height;
	(RecordedScene ? RecordedScene.get() : Commands->GetDrawCommands())->clearAttachments(1, &attachment, 1, &rect);
}

void VulkanRenderDevice::PushHit(const uint8_t* Data, int Count)
{
	if (Count <= 0) return;
	HitQueryStack.insert(HitQueryStack.end(), Data, Data + Count);

	SetHitLocation();
}

void VulkanRenderDevice::PopHit(int Count, bool bForce)
{
	if (bForce) // Force hit what we are popping
		ForceHitIndex = (int)HitQueries.size() - 1;

	HitQueryStack.resize(HitQueryStack.size() - Count);

	SetHitLocation();
}

void VulkanRenderDevice::SetHitLocation()
{
	DrawBatch(Commands->GetDrawCommands());

	if (!HitQueryStack.empty())
	{
		int index = (int)HitQueries.size();

		HitQuery query;
		query.Start = (int)HitBuffer.size();
		query.Count = (int)HitQueryStack.size();
		HitQueries.push_back(query);

		HitBuffer.insert(HitBuffer.end(), HitQueryStack.begin(), HitQueryStack.end());

		pushconstants.hitIndex = index + 1;
	}
	else
	{
		pushconstants.hitIndex = 0;
	}
}

void VulkanRenderDevice::ReadPixels(TextureColor* Pixels)
{
	auto cmdbuffer = Commands->GetDrawCommands();

	DrawBatch(cmdbuffer);

	if (GammaCorrectScreenshots)
	{
		PresentPushConstants pushconstants = GetPresentPushConstants();

		bool ActiveHdr = false; // (Commands->SwapChain->Format().colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) ? 1 : 0;

		// Select present shader based on what the user is actually using
		int presentShader = 0;
		if (ActiveHdr) presentShader |= 1;
		if (GammaMode == 1) presentShader |= 2;
		if (pushconstants.Brightness != 0.0f || pushconstants.Contrast != 1.0f || pushconstants.Saturation != 1.0f) presentShader |= (clamp(GrayFormula, 0, 2) + 1) << 2;

		VkViewport viewport = {};
		viewport.width = (float)Textures->Scene->Width;
		viewport.height = (float)Textures->Scene->Height;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.extent.width = Textures->Scene->Width;
		scissor.extent.height = Textures->Scene->Height;

		auto cmdbuffer = Commands->GetDrawCommands();

		VkAccessFlags srcColorAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		VkAccessFlags dstColorAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		PipelineBarrier()
			.AddImage(Textures->Scene->PPImage[1].get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcColorAccess, dstColorAccess)
			.Execute(cmdbuffer, srcStages, dstStages);

		RenderPassBegin()
			.RenderPass(RenderPasses->Postprocess.RenderPass.get())
			.Framebuffer(Framebuffers->PPImageFB[1].get())
			.RenderArea(0, 0, Textures->Scene->Width, Textures->Scene->Height)
			.AddClearColor(0.0f, 0.0f, 0.0f, 1.0f)
			.Execute(cmdbuffer);

		cmdbuffer->setViewport(0, 1, &viewport);
		cmdbuffer->setScissor(0, 1, &scissor);
		cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, RenderPasses->Present.ScreenshotPipeline[presentShader + (pushconstants.AmbientOcclusion > 0.0f ? (Textures->Scene->SceneSamples > VK_SAMPLE_COUNT_1_BIT ? 32 : 16) : 0)].get());
		cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, RenderPasses->Present.PipelineLayout.get(), 0, DescriptorSets->GetPresentSet());
		cmdbuffer->pushConstants(RenderPasses->Present.PipelineLayout.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentPushConstants), &pushconstants);
		cmdbuffer->draw(6, 1, 0, 0);

		cmdbuffer->endRenderPass();

		PipelineBarrier()
			.AddImage(Textures->Scene->PPImage[1].get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	// TextureColor uses RGBA. ReadPixels callers allocate the render dimensions,
	// which can be smaller than the native window when upscaling is active.
	auto srcimage = Textures->Scene->PPImage[GammaCorrectScreenshots ? 1 : 0].get();

	int w = GetRenderWidth();
	int h = GetRenderHeight();
	void* data = Pixels;

	auto dstimage = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Size(w, h)
		.DebugName("ReadPixelsDstImage")
		.Create(Device.get());

	PipelineBarrier()
		.AddImage(srcimage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.AddImage(dstimage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkImageBlit blit = {};
	blit.srcOffsets[0] = { 0, 0, 0 };
	blit.srcOffsets[1] = { srcimage->width, srcimage->height, 1 };
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.dstOffsets[0] = { 0, 0, 0 };
	blit.dstOffsets[1] = { dstimage->width, dstimage->height, 1 };
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	cmdbuffer->blitImage(
		srcimage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstimage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit, VK_FILTER_NEAREST);

	PipelineBarrier()
		.AddImage(srcimage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
		.AddImage(dstimage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	// Staging buffer for download
	auto staging = BufferBuilder()
		.Size(w * h * 4)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.DebugName("ReadPixelsStaging")
		.Create(Device.get());

	// Copy from image to buffer
	VkBufferImageCopy region = {};
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cmdbuffer->copyImageToBuffer(dstimage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);

	// Submit command buffers and wait for device to finish the work
	SubmitFrame(false, 0, 0, false);

	uint8_t* pixels = (uint8_t*)staging->Map(0, w * h * 4);
	memcpy(data, pixels, w * h * 4);
	staging->Unmap();
    if(Crest && commandline->HasArg("","--crest-dump")) {
        if(!commandline->HasArg("","--save-directory") || !commandline->HasArg("","--maxframes"))throw std::runtime_error("Crest readback requires an isolated bounded run");
        const size_t bytes=CrestWater::Size*CrestWater::Size*CrestWater::Cascades*4*sizeof(float);
        const size_t foamBytes=bytes/4;
        auto readback=BufferBuilder().Size(bytes*2+foamBytes).Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT,VMA_MEMORY_USAGE_GPU_TO_CPU).Create(Device.get());
        auto cmd=Commands->GetDrawCommands();
        PipelineBarrier barrier;
        for(int i:{0,7})barrier.AddImage(Crest->Images[i].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,CrestWater::Cascades);
        barrier.AddImage(Crest->FoamImages[0].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,CrestWater::Cascades);
        barrier.Execute(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};copy.imageExtent={CrestWater::Size,CrestWater::Size,1};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,CrestWater::Cascades};
        cmd->copyImageToBuffer(Crest->Images[0]->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback->buffer,1,&copy);
        copy.bufferOffset=bytes;cmd->copyImageToBuffer(Crest->Images[7]->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback->buffer,1,&copy);
        copy.bufferOffset=bytes*2;cmd->copyImageToBuffer(Crest->FoamImages[0]->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback->buffer,1,&copy);
        PipelineBarrier restore;
        restore.AddImage(Crest->FoamImages[0].get(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,CrestWater::Cascades);
        for(int i:{0,7})restore.AddImage(Crest->Images[i].get(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,CrestWater::Cascades);
        restore.Execute(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        SubmitFrame(false,0,0,false);
        auto values=static_cast<const char*>(readback->Map(0,bytes*2+foamBytes));
        std::ofstream file(commandline->GetArg("","--crest-dump"),std::ios::binary);file.write(values,bytes*2);if(!file)throw std::runtime_error("Crest readback write failed");
        std::ofstream foamFile(commandline->GetArg("","--crest-dump")+".foam",std::ios::binary);foamFile.write(values+bytes*2,foamBytes);if(!foamFile)throw std::runtime_error("Crest foam readback write failed");readback->Unmap();
        LogMessage("RUNE_CREST_READBACK bytes="+std::to_string(bytes*2)+" time="+std::to_string(Crest->LastTime)+" surfaces="+std::to_string(Crest->Meshes.size()));
    }
}

void VulkanRenderDevice::EndFlash()
{
	if (FlashScale != vec4(0.5f, 0.5f, 0.5f, 0.0f) || FlashFog != vec4(0.0f, 0.0f, 0.0f, 0.0f))
	{
		vec4 color(FlashFog.x, FlashFog.y, FlashFog.z, 1.0f - std::min(FlashScale.x * 2.0f, 1.0f));
		vec2 zero2(0.0f);
		ivec4 zero4(0);

		DrawBatch(Commands->GetDrawCommands());
		pushconstants.objectToProjection = mat4::identity();
		pushconstants.objectToView = mat4::identity();
		pushconstants.nearClip = vec4(0.0f, 0.0f, 0.0f, 0.0f);

		SetPipeline(RenderPasses->GetEndFlashPipeline());

		auto alloc = ReserveVertices(4, 6);
		if (alloc.vptr)
		{
			SceneVertex* vptr = alloc.vptr;
			uint32_t* iptr = alloc.iptr;
			uint32_t vpos = alloc.vpos;

			vptr[0] = { 0, vec3(-1.0f, -1.0f, 0.0f), zero2, zero2, zero2, zero2, color, zero4 };
			vptr[1] = { 0, vec3(1.0f, -1.0f, 0.0f), zero2, zero2, zero2, zero2, color, zero4 };
			vptr[2] = { 0, vec3(1.0f,  1.0f, 0.0f), zero2, zero2, zero2, zero2, color, zero4 };
			vptr[3] = { 0, vec3(-1.0f,  1.0f, 0.0f), zero2, zero2, zero2, zero2, color, zero4 };

			iptr[0] = vpos;
			iptr[1] = vpos + 1;
			iptr[2] = vpos + 2;
			iptr[3] = vpos;
			iptr[4] = vpos + 2;
			iptr[5] = vpos + 3;

			UseVertices(4, 6);
		}

		DrawBatch(Commands->GetDrawCommands());
		if (CurrentFrame)
			SetSceneNode(CurrentFrame);
	}
}

void VulkanRenderDevice::SetSceneNode(SceneNode* Frame)
{
	auto commands = Commands->GetDrawCommands();
	DrawBatch(commands);

	CurrentFrame = Frame;
    CurrentLightingCamera = (mat4::transpose(Frame->WorldToView) * vec4(-Frame->WorldToView[12],-Frame->WorldToView[13],-Frame->WorldToView[14],0)).xyz();
	Aspect = Frame->FY / Frame->FX;
	RProjZ = (float)std::tan(radians(Frame->FovAngle) * 0.5);
	RFX2 = 2.0f * RProjZ / Frame->FX;
	RFY2 = 2.0f * RProjZ * Aspect / Frame->FY;

	viewportdesc = {};
	viewportdesc.x = DrawingShadow ? float((ShadowSlot%RuneShadows::Columns)*ShadowSize) : (float)Frame->XB;
	viewportdesc.y = DrawingShadow ? float((ShadowSlot/RuneShadows::Columns)*ShadowSize) : (float)Frame->YB;
	viewportdesc.width = (float)Frame->X;
	viewportdesc.height = (float)Frame->Y;
	if(WaterReflectionActive) { viewportdesc.x=viewportdesc.y=0;viewportdesc.width=viewportdesc.height=WaterSize; }
	viewportdesc.minDepth = DrawingShadow ? 0.0f : 0.1f;
	viewportdesc.maxDepth = 1.0f;
	(RecordedScene ? RecordedScene.get() : commands)->setViewport(0, 1, &viewportdesc);

	pushconstants.objectToProjection = mat4::frustum(-RProjZ, RProjZ, -Aspect * RProjZ, Aspect * RProjZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);

	// TBD; do this or do like UE1 does and do the transform on the CPU?
	// maybe optionally do one or the other? transform on CPU can be super slow --Xaleros
	pushconstants.objectToProjection = (DrawingShadow ? Frame->Projection : pushconstants.objectToProjection) * Frame->WorldToView * Frame->ObjectToWorld;

	pushconstants.objectToView = Frame->WorldToView * Frame->ObjectToWorld;
	pushconstants.nearClip = Frame->NearClip;
}

void VulkanRenderDevice::PrecacheTexture(TextureInfo& Info, uint32_t PolyFlags)
{
	PolyFlags = ApplyPrecedenceRules(PolyFlags);
	Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));
}

void VulkanRenderDevice::ClearTextureCache()
{
    MaterialCache.clear();
	Commands->WaitForPending();
	DescriptorSets->ClearCache();
	Textures->ClearCache();
	Uploads->ClearCache();
}

void VulkanRenderDevice::PrepareResolveTarget(VulkanCommandBuffer* commands, bool continuation)
{
	if (!UsesInPassResolve()) return;
	// The final scene pass writes PPImage[0] directly. Continuations resolve the
	// complete preserved MSAA color again; never discard the color/depth source.
	const VkAccessFlags srcAccess = continuation ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT :
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	const VkPipelineStageFlags srcStages = continuation ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT :
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	PipelineBarrier().AddImage(Textures->Scene->PPImage[0].get(),
		continuation ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, srcAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		.Execute(commands, srcStages, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

void VulkanRenderDevice::BlitSceneToPostprocess()
{
	auto buffers = Textures->Scene.get();
	auto cmdbuffer = Commands->GetDrawCommands();

	const bool inpass = UsesInPassResolve();
	PipelineBarrier barrer0;
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (!inpass) barrer0.AddImage(
		buffers->ColorBuffer.get(),
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_ACCESS_TRANSFER_READ_BIT);
	if (HitData)
	{
		barrer0.AddImage(
			buffers->HitBuffer.get(),
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT);
		barrer0.AddImage(
			buffers->PPHitBuffer.get(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT);
		srcStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	if (!inpass) barrer0.AddImage(
		buffers->PPImage[0].get(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT);
	if (!inpass || HitData) barrer0.Execute(
		Commands->GetDrawCommands(),
		srcStageMask,
		VK_PIPELINE_STAGE_TRANSFER_BIT);

	if (buffers->SceneSamples != VK_SAMPLE_COUNT_1_BIT)
	{
		VkImageResolve resolve = {};
		resolve.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		resolve.srcSubresource.layerCount = 1;
		resolve.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		resolve.dstSubresource.layerCount = 1;
		resolve.extent = { (uint32_t)buffers->ColorBuffer->width, (uint32_t)buffers->ColorBuffer->height, 1 };
		if (!inpass) cmdbuffer->resolveImage(
			buffers->ColorBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			buffers->PPImage[0]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &resolve);
		if (HitData)
		{
			cmdbuffer->resolveImage(
				buffers->HitBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				buffers->PPHitBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &resolve);
		}
	}
	else
	{
		auto colorBuffer = buffers->ColorBuffer.get();
		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { colorBuffer->width, colorBuffer->height, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { colorBuffer->width, colorBuffer->height, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		cmdbuffer->blitImage(
			colorBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			buffers->PPImage[0]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_NEAREST);
		if (HitData)
		{
			VkImageCopy copy = {};
			copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.srcSubresource.layerCount = 1;
			copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.dstSubresource.layerCount = 1;
			copy.extent = { (uint32_t)colorBuffer->width, (uint32_t)colorBuffer->height, (uint32_t)1 };
			cmdbuffer->copyImage(
				buffers->HitBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				buffers->PPHitBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &copy);
		}
	}

	PipelineBarrier barrier1;
	barrier1.AddImage(
		buffers->PPImage[0].get(),
		inpass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		inpass ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT);
	if (HitData)
	{
		barrier1.AddImage(
			buffers->PPHitBuffer.get(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT);
	}
	barrier1.Execute(
		Commands->GetDrawCommands(),
		(inpass ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT) | (HitData ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0),
		HitData ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	if (HitData)
	{
		VkBufferImageCopy copy = {};
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset = { (int32_t)HitX, (int32_t)HitY, (int32_t)0 };
		copy.imageExtent = { (uint32_t)HitWidth, (uint32_t)HitHeight, (uint32_t)1 };
		cmdbuffer->copyImageToBuffer(buffers->PPHitBuffer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffers->StagingHitBuffer->buffer, 1, &copy);

		PipelineBarrier()
			.AddBuffer(buffers->StagingHitBuffer.get(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	}
}

void VulkanRenderDevice::RunBloomPass()
{
	float blurAmount = 0.6f + BloomAmount * (1.9f / 255.0f);
	BloomPushConstants pushconstants;
	// Extract only bright highlights, with a restrained contribution. The
	// original bloom path keeps its existing threshold/intensity defaults.
	if (VisualEnhanced)
	{
		blurAmount = 0.9f;
		pushconstants.Threshold = 0.85f;
		pushconstants.Intensity = 0.18f;
	}
	ComputeBlurSamples(7, blurAmount, pushconstants.SampleWeights);

	// The subtle preset only needs quarter-resolution glow; preserve the
	// original half-resolution bloom path when the preset is disabled.
	int firstLevel = VisualEnhanced ? 1 : 0;
	auto cmdbuffer = Commands->GetDrawCommands();

	PipelineBarrier()
		.AddImage(Textures->Scene->BloomBlurLevels[firstLevel].VTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	// Extract overbright pixels that we want to bloom:
	BloomStep(cmdbuffer,
		RenderPasses->Bloom.Extract.get(),
		DescriptorSets->GetBloomPPImageSet(),
		Framebuffers->BloomBlurLevels[firstLevel].VTextureFB.get(),
		Textures->Scene->BloomBlurLevels[firstLevel].Width,
		Textures->Scene->BloomBlurLevels[firstLevel].Height,
		pushconstants);

	// Blur and downscale:
	for (int i = firstLevel; i < NumBloomLevels - 1; i++)
	{
		PipelineBarrier()
			.AddImage(Textures->Scene->BloomBlurLevels[i].HTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// Gaussian blur
		BloomStep(cmdbuffer,
			RenderPasses->Bloom.BlurVertical.get(),
			DescriptorSets->GetBloomVTextureSet(i),
			Framebuffers->BloomBlurLevels[i].HTextureFB.get(),
			Textures->Scene->BloomBlurLevels[i].Width,
			Textures->Scene->BloomBlurLevels[i].Height,
			pushconstants);

		PipelineBarrier()
			.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			.AddImage(Textures->Scene->BloomBlurLevels[i].HTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		BloomStep(cmdbuffer,
			RenderPasses->Bloom.BlurHorizontal.get(),
			DescriptorSets->GetBloomHTextureSet(i),
			Framebuffers->BloomBlurLevels[i].VTextureFB.get(),
			Textures->Scene->BloomBlurLevels[i].Width,
			Textures->Scene->BloomBlurLevels[i].Height,
			pushconstants);

		PipelineBarrier()
			.AddImage(Textures->Scene->BloomBlurLevels[i + 1].VTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// Linear downscale
		BloomStep(cmdbuffer,
			RenderPasses->Bloom.Scale.get(),
			DescriptorSets->GetBloomVTextureSet(i),
			Framebuffers->BloomBlurLevels[i + 1].VTextureFB.get(),
			Textures->Scene->BloomBlurLevels[i + 1].Width,
			Textures->Scene->BloomBlurLevels[i + 1].Height,
			pushconstants);
	}

	// Blur and upscale:
	for (int i = NumBloomLevels - 1; i >= firstLevel; i--)
	{
		PipelineBarrier()
			.AddImage(Textures->Scene->BloomBlurLevels[i].HTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// Gaussian blur
		BloomStep(cmdbuffer,
			RenderPasses->Bloom.BlurVertical.get(),
			DescriptorSets->GetBloomVTextureSet(i),
			Framebuffers->BloomBlurLevels[i].HTextureFB.get(),
			Textures->Scene->BloomBlurLevels[i].Width,
			Textures->Scene->BloomBlurLevels[i].Height,
			pushconstants);

		PipelineBarrier()
			.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			.AddImage(Textures->Scene->BloomBlurLevels[i].HTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		BloomStep(cmdbuffer,
			RenderPasses->Bloom.BlurHorizontal.get(),
			DescriptorSets->GetBloomHTextureSet(i),
			Framebuffers->BloomBlurLevels[i].VTextureFB.get(),
			Textures->Scene->BloomBlurLevels[i].Width,
			Textures->Scene->BloomBlurLevels[i].Height,
			pushconstants);

		// Linear upscale
		if (i > firstLevel)
		{
			PipelineBarrier()
				.AddImage(Textures->Scene->BloomBlurLevels[i - 1].VTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
				.AddImage(Textures->Scene->BloomBlurLevels[i].VTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
				.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

			BloomStep(cmdbuffer,
				RenderPasses->Bloom.Scale.get(),
				DescriptorSets->GetBloomVTextureSet(i),
				Framebuffers->BloomBlurLevels[i - 1].VTextureFB.get(),
				Textures->Scene->BloomBlurLevels[i - 1].Width,
				Textures->Scene->BloomBlurLevels[i - 1].Height,
				pushconstants);
		}
	}

	PipelineBarrier()
		.AddImage(Textures->Scene->PPImage[0].get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
		.AddImage(Textures->Scene->BloomBlurLevels[firstLevel].VTexture.get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	// Add bloom back to frame post process texture:
	BloomStep(cmdbuffer,
		RenderPasses->Bloom.Combine.get(),
		DescriptorSets->GetBloomVTextureSet(firstLevel),
		Framebuffers->PPImageFB[0].get(),
		Textures->Scene->Width,
		Textures->Scene->Height,
		pushconstants);

	PipelineBarrier()
		.AddImage(Textures->Scene->PPImage[0].get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VulkanRenderDevice::BloomStep(VulkanCommandBuffer* cmdbuffer, VulkanPipeline* pipeline, VulkanDescriptorSet* input, VulkanFramebuffer* output, int width, int height, const BloomPushConstants& pushconstants)
{
	RenderPassBegin()
		.RenderPass(pipeline != RenderPasses->Bloom.Combine.get() ? RenderPasses->Postprocess.RenderPass.get() : RenderPasses->Postprocess.RenderPassCombine.get())
		.Framebuffer(output)
		.RenderArea(0, 0, width, height)
		.AddClearColor(0.0f, 0.0f, 0.0f, 1.0f)
		.Execute(cmdbuffer);

	VkViewport viewport = {};
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent.width = width;
	scissor.extent.height = height;

	cmdbuffer->setViewport(0, 1, &viewport);
	cmdbuffer->setScissor(0, 1, &scissor);
	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, RenderPasses->Bloom.PipelineLayout.get(), 0, input);
	cmdbuffer->pushConstants(RenderPasses->Bloom.PipelineLayout.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPushConstants), &pushconstants);
	cmdbuffer->draw(6, 1, 0, 0);

	cmdbuffer->endRenderPass();
}

float VulkanRenderDevice::ComputeBlurGaussian(float n, float theta) // theta = Blur Amount
{
	return (float)((1.0f / sqrt(2 * 3.14159265359f * theta)) * expf(-(n * n) / (2.0f * theta * theta)));
}

void VulkanRenderDevice::ComputeBlurSamples(int sampleCount, float blurAmount, float* sampleWeights)
{
	sampleWeights[0] = ComputeBlurGaussian(0, blurAmount);

	float totalWeights = sampleWeights[0];

	for (int i = 0; i < sampleCount / 2; i++)
	{
		float weight = ComputeBlurGaussian(i + 1.0f, blurAmount);

		sampleWeights[i * 2 + 1] = weight;
		sampleWeights[i * 2 + 2] = weight;

		totalWeights += weight * 2;
	}

	for (int i = 0; i < sampleCount; i++)
	{
		sampleWeights[i] /= totalWeights;
	}
}

PresentPushConstants VulkanRenderDevice::GetPresentPushConstants()
{
	PresentPushConstants pushconstants;
	pushconstants.SourceSize = vec4(1.0f / Textures->Scene->Width, 1.0f / Textures->Scene->Height, (float)Textures->Scene->Width, (float)Textures->Scene->Height);
	pushconstants.Upscale = UpscaleSGSR && ResolutionScale < 1.0f ? 1 : 0;
	pushconstants.AdaptiveSharpness = IsOrtho || pushconstants.Upscale ? 0.0f : clamp(AdaptiveSharpness, 0.0f, 1.0f);
	pushconstants.ShadowLift = IsOrtho ? 0.0f : clamp(ShadowLift, 0.0f, 0.65f);
	pushconstants.AmbientOcclusion = AmbientOcclusionMode() * 0.20f;
	pushconstants.AmbientSamples = 4;
    bool underwater=UsesModernWater() && WaterSubmerged && !RuneMenu::Open.load() && commandline->GetArgInt("","--water-underwater",1)!=0;
    pushconstants.Underwater=vec4(WaterViewTime,underwater?1.0f:0.0f,0,0);
	pushconstants.HdrScale = 0.8f + HdrScale * (3.0f / 255.0f);
	if (IsOrtho)
	{
		pushconstants.GammaCorrection = { 1.0f };
		pushconstants.Contrast = 1.0f;
		pushconstants.Saturation = 1.0f;
		pushconstants.Brightness = 0.0f;
	}
	else
	{
		float brightness = clamp(Brightness * 2.0f, 0.05f, 2.99f);

		if (GammaMode == 0)
		{
			float invGammaRed = 1.0f / std::max(brightness + GammaOffset + GammaOffsetRed, 0.001f);
			float invGammaGreen = 1.0f / std::max(brightness + GammaOffset + GammaOffsetGreen, 0.001f);
			float invGammaBlue = 1.0f / std::max(brightness + GammaOffset + GammaOffsetBlue, 0.001f);
			pushconstants.GammaCorrection = vec4(invGammaRed, invGammaGreen, invGammaBlue, 0.0f);
		}
		else
		{
			float invGammaRed = (GammaOffset + GammaOffsetRed + 2.0f) > 0.0f ? 1.0f / (GammaOffset + GammaOffsetRed + 1.0f) : 1.0f;
			float invGammaGreen = (GammaOffset + GammaOffsetGreen + 2.0f) > 0.0f ? 1.0f / (GammaOffset + GammaOffsetGreen + 1.0f) : 1.0f;
			float invGammaBlue = (GammaOffset + GammaOffsetBlue + 2.0f) > 0.0f ? 1.0f / (GammaOffset + GammaOffsetBlue + 1.0f) : 1.0f;
			pushconstants.GammaCorrection = vec4(invGammaRed, invGammaGreen, invGammaBlue, brightness);
		}

		// pushconstants.Contrast = clamp(Contrast, 0.1f, 3.f);
		if (Contrast >= 128)
		{
			pushconstants.Contrast = 1.0f + (Contrast - 128) / 127.0f * 3.0f;
		}
		else
		{
			pushconstants.Contrast = std::max(Contrast / 128.0f, 0.1f);
		}

		// pushconstants.Saturation = clamp(Saturation, -1.0f, 1.0f);
		pushconstants.Saturation = 1.0f - 2.0f * (255 - Saturation) / 255.0f;

		if (VisualEnhanced)
		{
			pushconstants.Contrast *= 1.035f;
			pushconstants.Saturation *= 0.97f;
		}

		// pushconstants.Brightness = clamp(LinearBrightness, -1.8f, 1.8f);
		if (LinearBrightness >= 128)
		{
			pushconstants.Brightness = (LinearBrightness - 128) / 127.0f * 1.8f;
		}
		else
		{
			pushconstants.Brightness = (128 - LinearBrightness) / 128.0f * -1.8f;
		}
	}
	return pushconstants;
}

void VulkanRenderDevice::DrawPresentTexture(int width, int height)
{
    Commands->SetProfilePass(CommandBufferManager::GpuPass::Present);
	int vpWidth = Viewport->GetNativePixelWidth();
	int vpHeight = Viewport->GetNativePixelHeight();

	PresentPushConstants pushconstants = GetPresentPushConstants();
	int sourceWidth = vpWidth;
	int sourceHeight = vpHeight;
#ifdef __ANDROID__
	// The scene and HUD stay upright; only the final full-screen sample is
	// rotated into the natural-orientation swapchain image.
	auto transform = Commands->SwapChain->Transform();
	if (transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)
	{
		pushconstants.TextureRow0 = vec4(0, 1, 0, 0);
		pushconstants.TextureRow1 = vec4(-1, 0, 1, 0);
		std::swap(sourceWidth, sourceHeight);
	}
	else if (transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
	{
		pushconstants.TextureRow0 = vec4(-1, 0, 1, 0);
		pushconstants.TextureRow1 = vec4(0, -1, 1, 0);
	}
	else if (transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
	{
		pushconstants.TextureRow0 = vec4(0, -1, 1, 0);
		pushconstants.TextureRow1 = vec4(1, 0, 0, 0);
		std::swap(sourceWidth, sourceHeight);
	}
#endif

	bool ActiveHdr = (Commands->SwapChain->Format().colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) ? 1 : 0;

	// Select present shader based on what the user is actually using
	int presentShader = 0;
	if (ActiveHdr) presentShader |= 1;
	if (GammaMode == 1) presentShader |= 2;
	if (pushconstants.Brightness != 0.0f || pushconstants.Contrast != 1.0f || pushconstants.Saturation != 1.0f) presentShader |= (clamp(GrayFormula, 0, 2) + 1) << 2;

	float scale = std::min(width / (float)sourceWidth, height / (float)sourceHeight);
	int letterboxWidth = (int)std::round(sourceWidth * scale);
	int letterboxHeight = (int)std::round(sourceHeight * scale);
	int letterboxX = (width - letterboxWidth) / 2;
	int letterboxY = (height - letterboxHeight) / 2;

	VkViewport viewport = {};
	viewport.x = (float)letterboxX;
	viewport.y = (float)letterboxY;
	viewport.width = (float)letterboxWidth;
	viewport.height = (float)letterboxHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset.x = letterboxX;
	scissor.offset.y = letterboxY;
	scissor.extent.width = letterboxWidth;
	scissor.extent.height = letterboxHeight;

	auto cmdbuffer = Commands->GetDrawCommands();

	PipelineBarrier()
		.AddImage(Commands->SwapChain->GetImage(Commands->PresentImageIndex), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	RenderPassBegin()
		.RenderPass(RenderPasses->Present.RenderPass.get())
		.Framebuffer(Framebuffers->GetSwapChainFramebuffer())
		.RenderArea(0, 0, Commands->SwapChain->Width(), Commands->SwapChain->Height())
		.AddClearColor(0.0f, 0.0f, 0.0f, 1.0f)
		.Execute(cmdbuffer);
	cmdbuffer->setViewport(0, 1, &viewport);
	cmdbuffer->setScissor(0, 1, &scissor);
	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, RenderPasses->Present.Pipeline[presentShader + (pushconstants.AmbientOcclusion > 0.0f ? (Textures->Scene->SceneSamples > VK_SAMPLE_COUNT_1_BIT ? 32 : 16) : 0)].get());
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, RenderPasses->Present.PipelineLayout.get(), 0, DescriptorSets->GetPresentSet());
	cmdbuffer->pushConstants(RenderPasses->Present.PipelineLayout.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentPushConstants), &pushconstants);
	cmdbuffer->draw(6, 1, 0, 0);
	cmdbuffer->endRenderPass();

	PipelineBarrier()
		.AddImage(Commands->SwapChain->GetImage(Commands->PresentImageIndex), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}

#include "VulkanValidationProbe.h"
#include "ShadowBudgetProbe.h"

#include "LightShadowPass.h"
#include "WaterReflectionPass.h"
