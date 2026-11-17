
#include "Precomp.h"
#include "RenderPassManager.h"
#include "VulkanRenderDevice.h"
#include "ShaderOptimizationDefaults.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <surrealgpu/vulkanbuilders.h>
#include <surrealgpu/vulkanswapchain.h>

#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Utils/File.h"
#include <chrono>
#include <fstream>
#include <unordered_map>

namespace {
using ScenePipelineSet = decltype(RenderPassManager::Scene)::PipelineSet;
struct SpecializedLightingPipelines { ScenePipelineSet sets[2]; };
// RenderDevice and its pipelines are used on the engine thread. Keep optional
// variants out of the public object layout and release them before its layouts.
std::unordered_map<const RenderPassManager*,std::unique_ptr<SpecializedLightingPipelines>> specializedLighting;
bool LightingSpecializationEnabled(VulkanRenderDevice* renderer)
{
    const int defaultEnabled = UsesQualifiedShaderDefaults(*renderer->Device) ? 1 : 0;
    return commandline->GetArgInt("","--render-specialized-lighting",defaultEnabled)!=0;
}
ScenePipelineSet& ActiveScenePipelines(RenderPassManager* owner,VulkanRenderDevice* renderer,
                                     ScenePipelineSet* generic)
{
    auto found=specializedLighting.find(owner);
    // Match the packet producer and the actual bound shadow-lighting data.
    // Other modes, including Original, continue through the generic shaders.
    bool eligible=(renderer->WorldFeatures&65536)!=0 && renderer->Buffers->ShadowLighting &&
        renderer->Buffers->ShadowLighting->CameraPosition.w>0.5f;
    if(eligible && found!=specializedLighting.end())return found->second->sets[renderer->OmitsHitOutput()];
    return generic[renderer->OmitsHitOutput()];
}
}


RenderPassManager::RenderPassManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
    const auto start = std::chrono::steady_clock::now();
#ifdef __ANDROID__
    const int defaultCache = UsesQualifiedShaderDefaults(*renderer->Device) ? 1 : 0;
#else
    constexpr int defaultCache = 0;
#endif
    if (commandline->GetArgInt("", "--render-pipeline-cache", defaultCache) != 0) {
        constexpr uintmax_t maxCacheBytes = 16 * 1024 * 1024;
        const auto& device = renderer->Device->PhysicalDevice.Properties.Properties;
        const auto file = "vulkan-pipelines-" + std::to_string(device.vendorID) + "-" +
            std::to_string(device.deviceID) + "-" + std::to_string(device.driverVersion) + ".bin";
        try {
            pipelineCachePath = Directory::localAppData() / "SurrealEngine" / file;
            std::error_code ec;
            const auto size = std::filesystem::file_size(pipelineCachePath, ec);
            PipelineCacheBuilder builder;
            if (!ec && size >= sizeof(VkPipelineCacheHeaderVersionOne) && size <= maxCacheBytes) {
                std::ifstream input(pipelineCachePath, std::ios::binary);
                std::vector<uint8_t> saved(static_cast<size_t>(size));
                if (input.read(reinterpret_cast<char*>(saved.data()), saved.size())) {
                    VkPipelineCacheHeaderVersionOne header{};
                    std::memcpy(&header, saved.data(), sizeof(header));
                    if (header.headerSize == sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                        header.vendorID == device.vendorID && header.deviceID == device.deviceID &&
                        std::memcmp(header.pipelineCacheUUID, device.pipelineCacheUUID, VK_UUID_SIZE) == 0) {
                        builder.InitialData(saved.data(), saved.size());
                        pipelineCacheBytesRead = saved.size();
                    }
                }
            }
            pipelineCache = builder.Create(renderer->Device.get());
        } catch (const std::exception&) {
            // Cache reading/creation is optional. Use the original uncached path.
            pipelineCache.reset();
            pipelineCacheBytesRead = 0;
        }
    }
	CreateSceneBindlessPipelineLayout();
	CreatePostprocessRenderPass();
	CreatePresentPipelineLayout();
	CreateScreenshotPipeline();
	CreateBloomPipelineLayout();
	CreateBloomPipeline();
    LogPipelineTiming("initial", start);
}

RenderPassManager::~RenderPassManager()
{
    specializedLighting.erase(this);
    if (!pipelineCache) return;
    try {
        const auto bytes = pipelineCache->GetCacheData();
        if (bytes.size() <= sizeof(VkPipelineCacheHeaderVersionOne) || bytes.size() > 16 * 1024 * 1024) return;
        std::filesystem::create_directories(pipelineCachePath.parent_path());
        auto temp = pipelineCachePath;
        temp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
        try {
            File::write_all_bytes(temp.string(), bytes.data(), bytes.size());
            std::filesystem::rename(temp, pipelineCachePath);
            LogMessage("RUNE_PIPELINE_CACHE_SAVED bytes=" + std::to_string(bytes.size()));
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
        }
    } catch (...) {
        // Driver serialization, low disk space and read-only profiles are nonfatal.
    }
}

void RenderPassManager::LogPipelineTiming(const char* phase, std::chrono::steady_clock::time_point start) {
    if (!commandline->HasArg("", "--frame-benchmark")) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    LogMessage("RUNE_PIPELINE_TIMING phase=" + std::string(phase) + " ms=" + std::to_string(ms) +
        " cache=" + std::to_string(bool(pipelineCache)) + " bytes_read=" + std::to_string(pipelineCacheBytesRead));
}

void RenderPassManager::CreateSceneBindlessPipelineLayout()
{
	Scene.BindlessPipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(renderer->DescriptorSets->GetTextureBindlessLayout())
		.AddSetLayout(renderer->DescriptorSets->GetMeshLightLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ScenePushConstants))
		.DebugName("SceneBindlessPipelineLayout")
		.Create(renderer->Device.get());
}

void RenderPassManager::CreatePresentPipelineLayout()
{
	Present.PipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(renderer->DescriptorSets->GetPresentLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentPushConstants))
		.DebugName("PresentPipelineLayout")
		.Create(renderer->Device.get());
}

void RenderPassManager::CreateBloomPipelineLayout()
{
	Bloom.PipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(renderer->DescriptorSets->GetBloomLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPushConstants))
		.DebugName("BloomPipelineLayout")
		.Create(renderer->Device.get());
}

PipelineState* RenderPassManager::GetPipeline(uint32_t PolyFlags, bool water, bool cameraFade)
{
    if(water) {
        auto& set=ActiveScenePipelines(this,renderer,Scene.Pipelines);
        if(renderer->UsesCrestWater())return &set.CrestOpaque;
        return (PolyFlags&PF_Highlighted)?&set.WaterTransparent:&set.WaterOpaque;
    }
	int index;
	if (PolyFlags & PF_Translucent)
	{
		index = 0;
	}
	else if (PolyFlags & PF_Modulated)
	{
		index = 1;
	}
	else if (PolyFlags & PF_Highlighted)
	{
		index = 2;
	}
	else
	{
		index = 3;
	}

	if (PolyFlags & PF_Invisible)
	{
		index |= 4;
	}
	if (PolyFlags & PF_Occlude)
	{
		index |= 8;
	}
	if (PolyFlags & PF_Masked)
	{
		index |= 16;
	}

	if (PolyFlags == PF_SubpixelFont)
	{
		index = 32;
	}

    // Coverage fading always writes surviving depth with opaque blending.
    // Masked coverage already uses the discard-capable alpha-test shader.
    if(cameraFade && !(PolyFlags&PF_Masked))
        return &ActiveScenePipelines(this,renderer,Scene.Pipelines).CameraFade[(PolyFlags&PF_Invisible)!=0];
	return &ActiveScenePipelines(this,renderer,Scene.Pipelines).Pipeline[index];
}

PipelineState* RenderPassManager::GetEndFlashPipeline()
{
	return &Scene.Pipelines[renderer->OmitsHitOutput()].Pipeline[2];
}

void RenderPassManager::CreatePipelines()
{
    const auto start = std::chrono::steady_clock::now();
	const bool enable=LightingSpecializationEnabled(renderer);
    if(enable && !specializedLighting.count(this))specializedLighting[this]=std::make_unique<SpecializedLightingPipelines>();
    if(!enable)specializedLighting.erase(this);
    LogMessage("RUNE_LIGHTING_SPECIALIZATION enabled="+std::to_string(enable));
    for(int specialized=0;specialized<(enable?2:1);++specialized)
    for (int noHit = 0; noHit < 2; noHit++)
	{
		auto& shaders = noHit ? renderer->Shaders->SceneNoHit : renderer->Shaders->Scene;
		auto& pipelines = specialized?specializedLighting.at(this)->sets[noHit]:Scene.Pipelines[noHit];
		const std::vector<uint32_t>& vertShader = shaders.VertexShader;
		const std::vector<uint32_t>& fragShader = shaders.FragmentShader;
		const std::vector<uint32_t>& fragShaderAlphaTest = shaders.FragmentShaderAlphaTest;
		VulkanPipelineLayout* layout = Scene.BindlessPipelineLayout.get();
		static const char* debugName = "ScenePipeline";

		for (int variant = 0; variant < (renderer->Crest?38:37); variant++)
        {
            const bool crest=variant==37;
            const bool water=variant==33 || variant==34 || crest;
            const bool cameraFade=variant>=35 && variant<37;
            const int i=water?(variant==33 || crest?11:2):cameraFade?(variant==35?11:15):variant;
            auto& activeShaders=crest?(noHit?renderer->Shaders->CrestSurfaceNoHit:renderer->Shaders->CrestSurface):water?(noHit?renderer->Shaders->WaterNoHit:renderer->Shaders->Water):shaders;
			GraphicsPipelineBuilder builder;
			builder.AddVertexShader(activeShaders.VertexShader).AddConstant(1002,uint32_t(specialized));
			builder.Viewport(0.0f, 0.0f, (float)renderer->Textures->Scene->Width, (float)renderer->Textures->Scene->Height);
			builder.Scissor(0, 0, renderer->Textures->Scene->Width, renderer->Textures->Scene->Height);
			builder.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
			builder.Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
			builder.AddVertexBufferBinding(0, sizeof(SceneVertex));
			builder.AddVertexAttribute(0, 0, VK_FORMAT_R32_UINT, offsetof(SceneVertex, Flags));
			builder.AddVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Position));
			builder.AddVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord));
			builder.AddVertexAttribute(3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord2));
			builder.AddVertexAttribute(4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord3));
			builder.AddVertexAttribute(5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord4));
			builder.AddVertexAttribute(6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, Color));
			builder.AddVertexAttribute(7, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(SceneVertex, TextureBinds));
			builder.AddVertexAttribute(8, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Normal));
			builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			builder.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
			builder.Layout(layout);
			builder.RenderPass(noHit ? Scene.RenderPassNoHit.get() : Scene.RenderPass.get());

			// Avoid clipping the weapon. The UE1 engine clips the geometry anyway.
			if (renderer->Device.get()->EnabledFeatures.Features.depthClamp)
				builder.DepthClampEnable(true);

			if (i < 32)
			{
				ColorBlendAttachmentBuilder colorblend;
				switch (i & 3)
				{
				case 0: // PF_Translucent
					colorblend.BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
					builder.DepthBias(true, -1.0f, 0.0f, -1.0f);
					break;
				case 1: // PF_Modulated
					colorblend.BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR);
					builder.DepthBias(true, -1.0f, 0.0f, -1.0f);
					break;
				case 2: // PF_Highlighted
					colorblend.BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
					builder.DepthBias(true, -1.0f, 0.0f, -1.0f);
					break;
				case 3:
					colorblend.BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO); // Hmm, is it faster to keep the blend mode enabled or to toggle it?
					break;
				}

				if (i & 4) // PF_Invisible
				{
					colorblend.ColorWriteMask(0);
				}

				if (i & 8) // PF_Occlude
				{
					builder.DepthStencilEnable(true, true, false);
				}
				else
				{
					builder.DepthStencilEnable(true, false, false);
				}

				if (i & 16) // PF_Masked
					builder.AddFragmentShader(activeShaders.FragmentShaderAlphaTest);
				else
					builder.AddFragmentShader(cameraFade?activeShaders.FragmentShaderCoverage:activeShaders.FragmentShader);

				builder.AddColorBlendAttachment(colorblend.Create());
				if (!noHit) builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().Create());
			}
			else // PF_SubpixelFont
			{
				builder.AddFragmentShader(activeShaders.FragmentShaderAlphaTest);
				builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder()
					.BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR)
					.Create());
				if (!noHit) builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().Create());
			}

			builder.AddConstant(1001,uint32_t(specialized)).AddConstant(1002,uint32_t(specialized));
            builder.RasterizationSamples(renderer->Textures->Scene->SceneSamples);
			builder.DebugName(debugName);

			auto& target=crest?pipelines.CrestOpaque:water?(variant==33?pipelines.WaterOpaque:pipelines.WaterTransparent):cameraFade?pipelines.CameraFade[variant-35]:pipelines.Pipeline[i];
            target.Pipeline = builder.Cache(pipelineCache.get()).Create(renderer->Device.get());
		}

		if(specialized)continue; // Lines/points keep the generic, mode-independent path.
		// Line pipeline
		for (int i = 0; i < 2; i++)
		{
			GraphicsPipelineBuilder builder;
			builder.AddVertexShader(vertShader);
			builder.Viewport(0.0f, 0.0f, (float)renderer->Textures->Scene->Width, (float)renderer->Textures->Scene->Height);
			builder.Scissor(0, 0, renderer->Textures->Scene->Width, renderer->Textures->Scene->Height);
			builder.Topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
			builder.Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
			builder.AddVertexBufferBinding(0, sizeof(SceneVertex));
			builder.AddVertexAttribute(0, 0, VK_FORMAT_R32_UINT, offsetof(SceneVertex, Flags));
			builder.AddVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Position));
			builder.AddVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord));
			builder.AddVertexAttribute(3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord2));
			builder.AddVertexAttribute(4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord3));
			builder.AddVertexAttribute(5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord4));
			builder.AddVertexAttribute(6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, Color));
			builder.AddVertexAttribute(7, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(SceneVertex, TextureBinds));
			builder.AddVertexAttribute(8, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Normal));
			builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			builder.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
			builder.Layout(layout);
			builder.RenderPass(noHit ? Scene.RenderPassNoHit.get() : Scene.RenderPass.get());

			builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA).Create());
			if (!noHit) builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().Create());

			builder.DepthStencilEnable(true, true, false);
			builder.AddFragmentShader(fragShader);

			builder.RasterizationSamples(renderer->Textures->Scene->SceneSamples);
			builder.DebugName(debugName);

			pipelines.LinePipeline[i].Pipeline = builder.Cache(pipelineCache.get()).Create(renderer->Device.get());

			if (i == 0)
			{
				pipelines.LinePipeline[i].MinDepth = 0.0f;
				pipelines.LinePipeline[i].MaxDepth = 0.1f;
			}
		}

		// Point pipeline
		for (int i = 0; i < 2; i++)
		{
			GraphicsPipelineBuilder builder;
			builder.AddVertexShader(vertShader);
			builder.AddFragmentShader(fragShader);
			builder.Viewport(0.0f, 0.0f, (float)renderer->Textures->Scene->Width, (float)renderer->Textures->Scene->Height);
			builder.Scissor(0, 0, renderer->Textures->Scene->Width, renderer->Textures->Scene->Height);
			builder.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
			builder.Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
			builder.AddVertexBufferBinding(0, sizeof(SceneVertex));
			builder.AddVertexAttribute(0, 0, VK_FORMAT_R32_UINT, offsetof(SceneVertex, Flags));
			builder.AddVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Position));
			builder.AddVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord));
			builder.AddVertexAttribute(3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord2));
			builder.AddVertexAttribute(4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord3));
			builder.AddVertexAttribute(5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord4));
			builder.AddVertexAttribute(6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, Color));
			builder.AddVertexAttribute(7, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(SceneVertex, TextureBinds));
			builder.AddVertexAttribute(8, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Normal));
			builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			builder.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
			builder.Layout(layout);
			builder.RenderPass(noHit ? Scene.RenderPassNoHit.get() : Scene.RenderPass.get());

			builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA).Create());
			if (!noHit) builder.AddColorBlendAttachment(ColorBlendAttachmentBuilder().Create());

			builder.DepthStencilEnable(true, true, false);
			builder.RasterizationSamples(renderer->Textures->Scene->SceneSamples);
			builder.DebugName(debugName);

			pipelines.PointPipeline[i].Pipeline = builder.Cache(pipelineCache.get()).Create(renderer->Device.get());

			if (i == 0)
			{
				pipelines.PointPipeline[i].MinDepth = 0.0f;
				pipelines.PointPipeline[i].MaxDepth = 0.1f;
			}
		}
	}
    LogPipelineTiming("scene", start);
}

PipelineState* RenderPassManager::GetLinePipeline(bool occludeLines)
{
	return &Scene.Pipelines[renderer->OmitsHitOutput()].LinePipeline[occludeLines];
}

PipelineState* RenderPassManager::GetPointPipeline(bool occludeLines)
{
	return &Scene.Pipelines[renderer->OmitsHitOutput()].PointPipeline[occludeLines];
}

VulkanRenderPass* RenderPassManager::GetScenePass(bool continuation, bool discardHit, bool noHit) const
{
	if (noHit) return continuation ? Scene.RenderPassContinueNoHit.get() : Scene.RenderPassNoHit.get();
	if (discardHit)
		return continuation ? Scene.RenderPassContinueDiscardHit.get() : Scene.RenderPassDiscardHit.get();
	return continuation ? Scene.RenderPassContinue.get() : Scene.RenderPass.get();
}

void RenderPassManager::CreateRenderPass()
{
	// Load/store operations do not affect render-pass compatibility. All four
	// variants share pipelines and framebuffer; selection frames retain hit data.
	auto create = [&](bool continuation, bool discardHit, bool noHit = false, bool discardColor = false, bool discardDepth = false)
	{
		const auto load = continuation ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		RenderPassBuilder builder;
		builder.AddAttachment(renderer->SceneColorFormat, renderer->Textures->Scene->SceneSamples,
				load, discardColor ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.AddAttachment(VK_FORMAT_R32_UINT, renderer->Textures->Scene->SceneSamples,
				noHit || (continuation && discardHit) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : load,
				discardHit ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.AddDepthStencilAttachment(VK_FORMAT_D32_SFLOAT, renderer->Textures->Scene->SceneSamples,
				load, discardDepth ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		if (renderer->UsesInPassResolve())
			builder.AddAttachment(renderer->SceneColorFormat, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		builder.AddSubpass()
			.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		if (!noHit) builder.AddSubpassColorAttachmentRef(1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		builder.AddSubpassDepthStencilAttachmentRef(2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		if (renderer->UsesInPassResolve())
			{
			builder.AddSubpassResolveAttachmentRef(3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			if (!noHit) builder.AddSubpassResolveAttachmentRef(VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED);
		}
		return builder.DebugName(continuation ? "SceneContinue" : "SceneClear")
			.Create(renderer->Device.get());
	};
	Scene.RenderPass = create(false, false);
	Scene.RenderPassContinue = create(true, false);
	Scene.RenderPassDiscardHit = create(false, true);
	Scene.RenderPassContinueDiscardHit = create(true, true);
	Scene.RenderPassNoHit = create(false, true, true);
	Scene.RenderPassContinueNoHit = create(true, true, true);
	if (renderer->SceneStoreMode == 1)
	{
		const bool discardColor = renderer->UsesInPassResolve();
		for (int i = 0; i < 2; ++i)
		{
			Scene.FinalNoHit[i] = create(i != 0, true, true, discardColor, true);
			Scene.FinalNoHitKeepDepth[i] = discardColor ? create(i != 0, true, true, true, false) : nullptr;
		}
	}
}

void RenderPassManager::CreatePresentRenderPass()
{
	Present.RenderPass = RenderPassBuilder()
		.AddAttachment(
			renderer->Commands->SwapChain->Format().format,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.DebugName("PresentRenderPass")
		.Create(renderer->Device.get());
}

void RenderPassManager::CreatePresentPipeline()
{
    const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 48; i++)
	{
		Present.Pipeline[i] = GraphicsPipelineBuilder()
			.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
			.AddFragmentShader(renderer->Shaders->Postprocess.FragmentPresentShader[i])
			.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
			.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
			.Layout(Present.PipelineLayout.get())
			.RenderPass(Present.RenderPass.get())
			.DebugName("PresentPipeline")
			.Cache(pipelineCache.get()).Create(renderer->Device.get());
	}
    LogPipelineTiming("present", start);
}

void RenderPassManager::CreateScreenshotPipeline()
{
	for (int i = 0; i < 48; i++)
	{
		Present.ScreenshotPipeline[i] = GraphicsPipelineBuilder()
			.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
			.AddFragmentShader(renderer->Shaders->Postprocess.FragmentPresentShader[i])
			.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
			.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
			.Layout(Present.PipelineLayout.get())
			.RenderPass(Postprocess.RenderPass.get())
			.DebugName("ScreenshotPipeline")
			.Cache(pipelineCache.get()).Create(renderer->Device.get());
	}
}

void RenderPassManager::CreatePostprocessRenderPass()
{
	Postprocess.RenderPass = RenderPassBuilder()
		.AddAttachment(
			renderer->SceneColorFormat,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.DebugName("PPRenderPass")
		.Create(renderer->Device.get());

	Postprocess.RenderPassCombine = RenderPassBuilder()
		.AddAttachment(
			renderer->SceneColorFormat,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.DebugName("PPRenderPassCombine")
		.Create(renderer->Device.get());
}

void RenderPassManager::CreateBloomPipeline()
{
	Bloom.Extract = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.Extract)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.Extract")
		.Cache(pipelineCache.get()).Create(renderer->Device.get());

	Bloom.Combine = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.Combine)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.AddColorBlendAttachment(ColorBlendAttachmentBuilder().BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE).Create())
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.Combine")
		.Cache(pipelineCache.get()).Create(renderer->Device.get());

	Bloom.Scale = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.Combine)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.Copy")
		.Cache(pipelineCache.get()).Create(renderer->Device.get());

	Bloom.BlurVertical = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.BlurVertical)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.BlurVertical")
		.Cache(pipelineCache.get()).Create(renderer->Device.get());

	Bloom.BlurHorizontal = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.BlurHorizontal)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.BlurHorizontal")
		.Cache(pipelineCache.get()).Create(renderer->Device.get());
}
