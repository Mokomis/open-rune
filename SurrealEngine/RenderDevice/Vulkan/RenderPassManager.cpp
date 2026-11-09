
#include "Precomp.h"
#include "RenderPassManager.h"
#include "VulkanRenderDevice.h"
#include "ShaderOptimizationDefaults.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <surrealgpu/vulkanbuilders.h>
#include <surrealgpu/vulkanswapchain.h>

#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
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
	CreateSceneBindlessPipelineLayout();
	CreatePostprocessRenderPass();
	CreatePresentPipelineLayout();
	CreateScreenshotPipeline();
	CreateBloomPipelineLayout();
	CreateBloomPipeline();
}

RenderPassManager::~RenderPassManager()
{
    specializedLighting.erase(this);
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

		for (int variant = 0; variant < 37; variant++)
        {
            const bool water=variant==33 || variant==34;
            const bool cameraFade=variant>=35;
            const int i=water?(variant==33?11:2):cameraFade?(variant==35?11:15):variant;
            auto& activeShaders=water?(noHit?renderer->Shaders->WaterNoHit:renderer->Shaders->Water):shaders;
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

			auto& target=water?(variant==33?pipelines.WaterOpaque:pipelines.WaterTransparent):cameraFade?pipelines.CameraFade[variant-35]:pipelines.Pipeline[i];
            target.Pipeline = builder.Create(renderer->Device.get());
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

			pipelines.LinePipeline[i].Pipeline = builder.Create(renderer->Device.get());

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

			pipelines.PointPipeline[i].Pipeline = builder.Create(renderer->Device.get());

			if (i == 0)
			{
				pipelines.PointPipeline[i].MinDepth = 0.0f;
				pipelines.PointPipeline[i].MaxDepth = 0.1f;
			}
		}
	}
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
	auto create = [&](bool continuation, bool discardHit, bool noHit = false)
	{
		const auto load = continuation ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		RenderPassBuilder builder;
		builder.AddAttachment(renderer->SceneColorFormat, renderer->Textures->Scene->SceneSamples,
				load, VK_ATTACHMENT_STORE_OP_STORE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.AddAttachment(VK_FORMAT_R32_UINT, renderer->Textures->Scene->SceneSamples,
				noHit || (continuation && discardHit) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : load,
				discardHit ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.AddDepthStencilAttachment(VK_FORMAT_D32_SFLOAT, renderer->Textures->Scene->SceneSamples,
				load, VK_ATTACHMENT_STORE_OP_STORE,
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
			.Create(renderer->Device.get());
	}
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
			.Create(renderer->Device.get());
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
		.Create(renderer->Device.get());

	Bloom.Combine = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.Combine)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.AddColorBlendAttachment(ColorBlendAttachmentBuilder().BlendMode(VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE).Create())
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.Combine")
		.Create(renderer->Device.get());

	Bloom.Scale = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.Combine)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.Copy")
		.Create(renderer->Device.get());

	Bloom.BlurVertical = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.BlurVertical)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.BlurVertical")
		.Create(renderer->Device.get());

	Bloom.BlurHorizontal = GraphicsPipelineBuilder()
		.AddVertexShader(renderer->Shaders->Postprocess.VertexShader)
		.AddFragmentShader(renderer->Shaders->Bloom.BlurHorizontal)
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.Layout(Bloom.PipelineLayout.get())
		.RenderPass(Postprocess.RenderPass.get())
		.DebugName("Bloom.BlurHorizontal")
		.Create(renderer->Device.get());
}
