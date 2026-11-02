#pragma once

#include <surrealgpu/vulkanobjects.h>

class VulkanRenderDevice;

struct PipelineState
{
	std::unique_ptr<VulkanPipeline> Pipeline;
	float MinDepth = 0.1f;
	float MaxDepth = 1.0f;
};

class RenderPassManager
{
public:
	RenderPassManager(VulkanRenderDevice* renderer);
	~RenderPassManager();

	void CreateRenderPass();
	void CreatePipelines();
	VulkanRenderPass* GetScenePass(bool continuation, bool discardHit, bool noHit = false) const;

	void CreatePresentRenderPass();
	void CreatePresentPipeline();
	void CreateScreenshotPipeline();

	void CreatePostprocessRenderPass();
	void CreateBloomPipeline();

	PipelineState* GetPipeline(uint32_t polyflags, bool water = false, bool cameraFade = false);
	PipelineState* GetEndFlashPipeline();
	PipelineState* GetLinePipeline(bool occludeLines);
	PipelineState* GetPointPipeline(bool occludeLines);

	struct
	{
		std::unique_ptr<VulkanPipelineLayout> BindlessPipelineLayout;
		std::unique_ptr<VulkanRenderPass> RenderPass;
		std::unique_ptr<VulkanRenderPass> RenderPassContinue;
		std::unique_ptr<VulkanRenderPass> RenderPassDiscardHit;
		std::unique_ptr<VulkanRenderPass> RenderPassContinueDiscardHit;
		std::unique_ptr<VulkanRenderPass> RenderPassNoHit;
		std::unique_ptr<VulkanRenderPass> RenderPassContinueNoHit;
		struct PipelineSet
		{
			PipelineState Pipeline[33];
            PipelineState WaterOpaque, WaterTransparent, CameraFade[2];
			PipelineState LinePipeline[2];
			PipelineState PointPipeline[2];
		} Pipelines[2];
	} Scene;

	struct
	{
		std::unique_ptr<VulkanPipelineLayout> PipelineLayout;
		std::unique_ptr<VulkanRenderPass> RenderPass;
		std::unique_ptr<VulkanPipeline> Pipeline[48];
		std::unique_ptr<VulkanPipeline> ScreenshotPipeline[48];
	} Present;

	struct
	{
		std::unique_ptr<VulkanPipelineLayout> PipelineLayout;
		std::unique_ptr<VulkanPipeline> Extract;
		std::unique_ptr<VulkanPipeline> Combine;
		std::unique_ptr<VulkanPipeline> Scale;
		std::unique_ptr<VulkanPipeline> BlurVertical;
		std::unique_ptr<VulkanPipeline> BlurHorizontal;
	} Bloom;

	struct
	{
		std::unique_ptr<VulkanRenderPass> RenderPass;
		std::unique_ptr<VulkanRenderPass> RenderPassCombine;
	} Postprocess;

private:
	void CreateSceneBindlessPipelineLayout();
	void CreatePresentPipelineLayout();
	void CreateBloomPipelineLayout();

	VulkanRenderDevice* renderer = nullptr;
};
