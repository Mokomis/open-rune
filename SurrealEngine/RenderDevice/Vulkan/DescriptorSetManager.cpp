
#include "Precomp.h"
#include "DescriptorSetManager.h"
#include "VulkanRenderDevice.h"
#include "CachedTexture.h"
#include "CrestWater.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Resources/Level/UModel.h"

DescriptorSetManager::DescriptorSetManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
	CreateBindlessTextureSet();
	CreateMeshLightSets();
	CreatePresentLayout();
	CreatePresentSet();
	CreateBloomLayout();
	CreateBloomSets();
}

DescriptorSetManager::~DescriptorSetManager()
{
	if (renderer->Buffers) { renderer->Buffers->MeshLightSet.reset(); renderer->Buffers->ReflectionLightSet.reset(); }
	if (renderer->AlternateBuffers) { renderer->AlternateBuffers->MeshLightSet.reset(); renderer->AlternateBuffers->ReflectionLightSet.reset(); }
}

void DescriptorSetManager::ClearCache()
{
	Textures.WriteBindless = WriteDescriptors();
	Textures.NextBindlessIndex = 0;
}

int DescriptorSetManager::GetTextureArrayIndex(uint32_t PolyFlags, CachedTexture* tex, bool clamp)
{
	if (Textures.NextBindlessIndex == MaxBindlessTextures)
	{
		static bool firstCall = true;
		if (firstCall)
		{
			LogMessage("============================================================================");
			LogMessage("VulkanDrv encountered more than " + std::to_string(MaxBindlessTextures) + " textures");
			LogMessage("============================================================================");
			firstCall = false;
		}
		return 0; // Oh oh, we are out of texture slots
	}

	if (Textures.NextBindlessIndex == 0)
	{
		Textures.WriteBindless.AddCombinedImageSampler(Textures.BindlessSet.get(), 0, 0, renderer->Textures->NullTextureView.get(), renderer->Samplers->Samplers[0].get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Textures.NextBindlessIndex = 1;
	}

	if (!tex)
		return 0;

	uint32_t samplermode = 0;
	if (PolyFlags & PF_NoSmooth) samplermode |= 1;
	if (clamp) samplermode |= 2;

	int index = tex->BindlessIndex[samplermode];
	if (index != -1)
		return index;

	index = Textures.NextBindlessIndex++;

	VulkanSampler* sampler = renderer->Samplers->Samplers[samplermode].get();
	Textures.WriteBindless.AddCombinedImageSampler(Textures.BindlessSet.get(), 0, index, tex->imageView.get(), sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	tex->BindlessIndex[samplermode] = index;
	return index;
}

void DescriptorSetManager::UpdateBindlessSet()
{
	Textures.WriteBindless.Execute(renderer->Device.get());
	Textures.WriteBindless = WriteDescriptors();
}

void DescriptorSetManager::CreateBindlessTextureSet()
{
	Textures.BindlessPool = DescriptorPoolBuilder()
		.Flags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxBindlessTextures)
		.MaxSets(MaxBindlessTextures)
		.DebugName("TextureBindlessPool")
		.Create(renderer->Device.get());

	Textures.BindlessLayout = DescriptorSetLayoutBuilder()
		.Flags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT)
		.AddBinding(
			0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			MaxBindlessTextures,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT)
		.DebugName("TextureBindlessLayout")
		.Create(renderer->Device.get());

	Textures.BindlessSet = Textures.BindlessPool->allocate(Textures.BindlessLayout.get(), MaxBindlessTextures);
}

void DescriptorSetManager::CreateMeshLightSets()
{
    const auto shadowType=renderer->UniformShadowData?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	MeshLightLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, shadowType, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(5,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(6,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddBinding(7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT)
        .DebugName("MeshLightLayout").Create(renderer->Device.get());
	MeshLightPool = DescriptorPoolBuilder().AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8).AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4).AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 24)
		.MaxSets(4).DebugName("MeshLightPool").Create(renderer->Device.get());
	for (auto* buffers : { renderer->Buffers.get(), renderer->AlternateBuffers.get() })
	{
		if (!buffers) continue;
		buffers->MeshLightSet = MeshLightPool->allocate(MeshLightLayout.get());
        buffers->ReflectionLightSet = MeshLightPool->allocate(MeshLightLayout.get());
        for(auto* set : {buffers->MeshLightSet.get(),buffers->ReflectionLightSet.get()})
        WriteDescriptors().AddBuffer(set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			buffers->MeshLightBuffer.get())
            .AddBuffer(set, 1, shadowType, buffers->ShadowLightingBuffer.get())
            .AddCombinedImageSampler(set, 2, renderer->ShadowView.get(), renderer->ShadowSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .AddCombinedImageSampler(set, 3, set==buffers->MeshLightSet.get()?renderer->WaterView.get():renderer->Textures->NullTextureView.get(), renderer->Samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .AddCombinedImageSampler(set,4,renderer->Crest?renderer->Crest->Views[7].get():renderer->Textures->NullTextureView.get(),renderer->Crest?renderer->Crest->Sampler.get():renderer->Samplers->PPLinearClamp.get(),renderer->Crest?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .AddCombinedImageSampler(set,5,renderer->Crest?renderer->Crest->FoamViews[0].get():renderer->Textures->NullTextureView.get(),renderer->Crest?renderer->Crest->Sampler.get():renderer->Samplers->PPLinearClamp.get(),renderer->Crest?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).Execute(renderer->Device.get());
	}
}

void DescriptorSetManager::CreatePresentLayout()
{
	Present.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("PresentLayout")
		.Create(renderer->Device.get());
}

void DescriptorSetManager::CreatePresentSet()
{
	Present.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3)
		.MaxSets(1)
		.DebugName("PresentPool")
		.Create(renderer->Device.get());
	Present.Set = Present.Pool->allocate(Present.Layout.get());
}

void DescriptorSetManager::CreateBloomLayout()
{
	Bloom.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("BloomLayout")
		.Create(renderer->Device.get());
}

void DescriptorSetManager::CreateBloomSets()
{
	Bloom.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (NumBloomLevels * 2 + 1) * 2)
		.MaxSets(NumBloomLevels * 2 + 1)
		.DebugName("BloomPool")
		.Create(renderer->Device.get());

	for (int level = 0; level < NumBloomLevels; level++)
	{
		Bloom.HTextureSets[level] = Bloom.Pool->allocate(Bloom.Layout.get());
		Bloom.VTextureSets[level] = Bloom.Pool->allocate(Bloom.Layout.get());
	}

	Bloom.PPImageSet = Bloom.Pool->allocate(Bloom.Layout.get());
}

void DescriptorSetManager::UpdateFrameDescriptors()
{
	auto textures = renderer->Textures.get();
	auto samplers = renderer->Samplers.get();

    if(renderer->Crest) {
        auto* c=renderer->Crest.get();auto* scene=textures->Scene.get();
        c->CreateCapture(renderer->Device.get(),scene->DepthBufferView.get(),scene->Width,scene->Height,renderer->SceneColorFormat,scene->SceneSamples);
        for(auto* buffers:{renderer->Buffers.get(),renderer->AlternateBuffers.get()})if(buffers)
            for(auto* set:{buffers->MeshLightSet.get(),buffers->ReflectionLightSet.get()})
                WriteDescriptors().AddCombinedImageSampler(set,6,c->BackgroundView.get(),renderer->Samplers->PPLinearClamp.get(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .AddCombinedImageSampler(set,7,c->SceneDepthView.get(),renderer->Samplers->PPNearestRepeat.get(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).Execute(renderer->Device.get());
    }
	WriteDescriptors write;
	write.AddCombinedImageSampler(Present.Set.get(), 0, textures->Scene->PPImageView[0].get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(Present.Set.get(), 1, textures->DitherImageView.get(), samplers->PPNearestRepeat.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(Present.Set.get(), 2, textures->Scene->DepthBufferView.get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	for (int level = 0; level < NumBloomLevels; level++)
	{
		write.AddCombinedImageSampler(GetBloomHTextureSet(level), 0, textures->Scene->BloomBlurLevels[level].HTextureView.get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		write.AddCombinedImageSampler(GetBloomVTextureSet(level), 0, textures->Scene->BloomBlurLevels[level].VTextureView.get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	write.AddCombinedImageSampler(Bloom.PPImageSet.get(), 0, textures->Scene->PPImageView[0].get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.Execute(renderer->Device.get());
}
