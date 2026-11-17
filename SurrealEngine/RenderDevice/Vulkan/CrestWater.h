#pragma once
#include <surrealgpu/vulkanbuilders.h>
#include "crest/CrestShaders.h"
#include "crest/CrestFoam.h"
#include <array>
#include <vector>
#include <unordered_map>
#include <cstring>

// Native scheduling for Crest 4.23.0's spectrum and two-axis FFT. No Unity runtime.
// Images stay GENERAL: compute and graphics use explicit read/write barriers.
class CrestWater
{
public:
    static constexpr int Size=128, Cascades=16;
    struct Parameters { uint32_t size=Size;float wind=6,turbulence=.25f,gravity=9.81f,period=0,time=0,chop=1.1f,dt=0;float windX=.8f,windY=.6f; };
    static_assert(sizeof(Parameters)==40);
    std::array<std::unique_ptr<VulkanImage>,8> Images;
    std::array<std::unique_ptr<VulkanImageView>,8> Views;
    std::unique_ptr<VulkanSampler> Sampler;
    std::unique_ptr<VulkanImageView> OutputStorageView;
    std::array<std::unique_ptr<VulkanDescriptorSetLayout>,4> SetLayouts;
    std::array<std::unique_ptr<VulkanPipelineLayout>,4> Layouts;
    std::unique_ptr<VulkanDescriptorPool> Pool;
    std::array<std::unique_ptr<VulkanDescriptorSet>,4> Sets;
    std::array<std::unique_ptr<VulkanPipeline>,4> Pipelines;
    bool Initialized=false;
    float LastTime=-1;
    uint64_t Frames=0;
    CrestWater(VulkanDevice* device)
    {
        if(!device->EnabledFeatures.Features.shaderStorageImageExtendedFormats)throw std::runtime_error("Crest needs extended storage image formats");
        for(int i=0;i<8;++i) {
            auto format=(i==0||i==7)?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32G32_SFLOAT;
            VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(device->PhysicalDevice.Device,format,&props);
            auto need=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if(i==7)need|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
            if((props.optimalTilingFeatures&need)!=need)throw std::runtime_error("Crest needs sampled/storage float image support");
            Images[i]=ImageBuilder().Size(Size,Size,i==7?8:1,Cascades).Format(format)
                .Usage(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT).DebugName("CrestFFT").Create(device);
            Views[i]=ImageViewBuilder().Type(VK_IMAGE_VIEW_TYPE_2D_ARRAY).Image(Images[i].get(),format,VK_IMAGE_ASPECT_COLOR_BIT).Create(device);
        }
        OutputStorageView=ImageViewBuilder().Type(VK_IMAGE_VIEW_TYPE_2D_ARRAY).Image(Images[7].get(),VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,0,0,1,Cascades).Create(device);
        Sampler=SamplerBuilder().AddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT).MinFilter(VK_FILTER_LINEAR).MagFilter(VK_FILTER_LINEAR).MaxLod(7).MipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR).Create(device);
        Pool=DescriptorPoolBuilder().AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,7).AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,8).MaxSets(4).Create(device);
        const int counts[4]={1,4,6,4};
        for(int p=0;p<4;++p) {
            DescriptorSetLayoutBuilder builder;
            for(int b=0;b<counts[p];++b)builder.AddBinding(b,(p==1?b==0:p>=2?b<3:false)?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT);
            SetLayouts[p]=builder.Create(device);Layouts[p]=PipelineLayoutBuilder().AddSetLayout(SetLayouts[p].get()).AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)).Create(device);
            Sets[p]=Pool->allocate(SetLayouts[p].get());
        }
        auto sampled=[&](int p,int b,int i) {
            VkDescriptorImageInfo image{};image.imageView=Views[i]->view;image.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=Sets[p]->set;write.dstBinding=b;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;
            vkUpdateDescriptorSets(device->device,1,&write,0,nullptr);
        };
        WriteDescriptors().AddStorageImage(Sets[0].get(),0,Views[0].get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        sampled(1,0,0);for(int i=0;i<3;++i)WriteDescriptors().AddStorageImage(Sets[1].get(),i+1,Views[i+1].get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        for(int p=2;p<4;++p)for(int i=0;i<3;++i)sampled(p,i,(p==2?1:4)+i);
        for(int i=0;i<3;++i)WriteDescriptors().AddStorageImage(Sets[2].get(),i+3,Views[i+4].get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        WriteDescriptors().AddStorageImage(Sets[3].get(),3,OutputStorageView.get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        const std::vector<uint32_t> codes[4]={
            {std::begin(CrestShaders::SpectrumInit),std::end(CrestShaders::SpectrumInit)},
            {std::begin(CrestShaders::SpectrumUpdate),std::end(CrestShaders::SpectrumUpdate)},
            {std::begin(CrestShaders::FFTX),std::end(CrestShaders::FFTX)},
            {std::begin(CrestShaders::FFTY),std::end(CrestShaders::FFTY)}};
        for(int p=0;p<4;++p)Pipelines[p]=ComputePipelineBuilder().Layout(Layouts[p].get()).ComputeShader(codes[p]).DebugName("CrestCompute").Create(device);
        CreateFoam(device);
    }
    void Barrier(VulkanCommandBuffer* cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags from,VkAccessFlags to)
    {
        PipelineBarrier b;for(int i=0;i<8;++i)b.AddImage(Images[i].get(),Initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,from,to,VK_IMAGE_ASPECT_COLOR_BIT,0,i==7?8:1,0,Cascades);
        b.Execute(cmd,src,dst);
    }
    void Update(VulkanCommandBuffer* cmd,float time)
    {
        Parameters pc;pc.time=time;pc.dt=LastTime<0?0:std::clamp(time-LastTime,0.f,.1f);
        Barrier(cmd,Initialized?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            Initialized?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        auto run=[&](int p,int x,int y,int z) {cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,Pipelines[p].get());cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,Layouts[p].get(),0,Sets[p].get());cmd->pushConstants(Layouts[p].get(),VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),&pc);cmd->dispatch(x,y,z);};
        if(!Initialized) {run(0,Size/8,Size/8,Cascades);Initialized=true;Barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);}
        run(1,Size/8,Size/8,Cascades);
        Barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        run(2,1,Size,Cascades);
        Barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        run(3,Size,1,Cascades);
        // Filter high frequencies before distant pixels sample the FFT field.
        PipelineBarrier().AddImage(Images[7].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,8,0,Cascades)
            .Execute(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        for(int mip=1;mip<8;++mip) {
            VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,uint32_t(mip-1),0,Cascades};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,uint32_t(mip),0,Cascades};
            blit.srcOffsets[1]={Size>>(mip-1),Size>>(mip-1),1};blit.dstOffsets[1]={Size>>mip,Size>>mip,1};
            cmd->blitImage(Images[7]->image,VK_IMAGE_LAYOUT_GENERAL,Images[7]->image,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_LINEAR);
            PipelineBarrier().AddImage(Images[7].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,mip,1,0,Cascades).Execute(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        }
        PipelineBarrier().AddImage(Images[7].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,8,0,Cascades)
            .Execute(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        UpdateFoam(cmd,pc);
        LastTime=time;++Frames;
    }
    std::array<std::unique_ptr<VulkanImage>,2> FoamImages;
    std::array<std::unique_ptr<VulkanImageView>,2> FoamViews;
    std::unique_ptr<VulkanDescriptorSetLayout> FoamSetLayout;
    std::unique_ptr<VulkanPipelineLayout> FoamLayout;
    std::unique_ptr<VulkanDescriptorPool> FoamPool;
    std::unique_ptr<VulkanDescriptorSet> FoamSet;
    std::unique_ptr<VulkanPipeline> FoamPipeline;
    bool FoamInitialized=false;
    void CreateFoam(VulkanDevice* device)
    {
        for(int i=0;i<2;++i){FoamImages[i]=ImageBuilder().Size(Size,Size,1,Cascades).Format(VK_FORMAT_R32_SFLOAT).Usage(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT).Create(device);
            FoamViews[i]=ImageViewBuilder().Type(VK_IMAGE_VIEW_TYPE_2D_ARRAY).Image(FoamImages[i].get(),VK_FORMAT_R32_SFLOAT).Create(device);}
        FoamSetLayout=DescriptorSetLayoutBuilder().AddBinding(0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT).AddBinding(1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT).AddBinding(2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT).Create(device);
        FoamLayout=PipelineLayoutBuilder().AddSetLayout(FoamSetLayout.get()).AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)).Create(device);
        FoamPool=DescriptorPoolBuilder().AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2).AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1).MaxSets(1).Create(device);FoamSet=FoamPool->allocate(FoamSetLayout.get());
        WriteDescriptors().AddCombinedImageSampler(FoamSet.get(),0,Views[7].get(),Sampler.get(),VK_IMAGE_LAYOUT_GENERAL).AddCombinedImageSampler(FoamSet.get(),1,FoamViews[1].get(),Sampler.get(),VK_IMAGE_LAYOUT_GENERAL).AddStorageImage(FoamSet.get(),2,FoamViews[0].get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        auto code=GLSLCompiler().Type(ShaderType::Compute).AddSource("CrestFoam",CrestFoamShader).Compile(device);FoamPipeline=ComputePipelineBuilder().Layout(FoamLayout.get()).ComputeShader(code).Create(device);
    }
    void UpdateFoam(VulkanCommandBuffer* cmd,const Parameters& pc)
    {
        bool reset=!FoamInitialized || pc.time<LastTime;
        PipelineBarrier barrier;for(auto& image:FoamImages)barrier.AddImage(image.get(),FoamInitialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,FoamInitialized?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT:0,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,Cascades);
        barrier.Execute(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        if(reset){VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,Cascades};for(auto& image:FoamImages)cmd->clearColorImage(image->image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);}
        else {VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,Cascades};copy.dstSubresource=copy.srcSubresource;copy.extent={Size,Size,1};cmd->copyImage(FoamImages[0]->image,VK_IMAGE_LAYOUT_GENERAL,FoamImages[1]->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);}
        PipelineBarrier ready;for(auto& image:FoamImages)ready.AddImage(image.get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,Cascades);
        ready.AddImage(Images[7].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,8,0,Cascades).Execute(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,FoamPipeline.get());cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,FoamLayout.get(),0,FoamSet.get());cmd->pushConstants(FoamLayout.get(),VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),&pc);cmd->dispatch(Size/8,Size/8,Cascades);
        PipelineBarrier().AddImage(FoamImages[0].get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,Cascades).Execute(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        FoamInitialized=true;
    }
    std::unique_ptr<VulkanSampler> DepthSampler;
    std::unique_ptr<VulkanImage> Background,SceneDepth;
    std::unique_ptr<VulkanImageView> BackgroundView,SceneDepthView;
    std::unique_ptr<VulkanDescriptorSetLayout> DepthSetLayout;
    std::unique_ptr<VulkanPipelineLayout> DepthLayout;
    std::unique_ptr<VulkanDescriptorPool> DepthPool;
    std::unique_ptr<VulkanDescriptorSet> DepthSet;
    std::unique_ptr<VulkanPipeline> DepthPipeline;
    bool Captured=false;
    void CreateCapture(VulkanDevice* device,VulkanImageView* depth,int width,int height,VkFormat colorFormat,VkSampleCountFlagBits samples)
    {
        DepthSet.reset();DepthPool.reset();DepthPipeline.reset();DepthLayout.reset();DepthSetLayout.reset();
        BackgroundView.reset();SceneDepthView.reset();Background.reset();SceneDepth.reset();Captured=false;
        Background=ImageBuilder().Size(width,height).Format(colorFormat).Usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT).Create(device);
        BackgroundView=ImageViewBuilder().Image(Background.get(),colorFormat).Create(device);
        SceneDepth=ImageBuilder().Size(width,height).Format(VK_FORMAT_R32_SFLOAT).Usage(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT).Create(device);
        SceneDepthView=ImageViewBuilder().Image(SceneDepth.get(),VK_FORMAT_R32_SFLOAT).Create(device);
        DepthSetLayout=DescriptorSetLayoutBuilder().AddBinding(0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT).AddBinding(1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT).Create(device);
        DepthLayout=PipelineLayoutBuilder().AddSetLayout(DepthSetLayout.get()).Create(device);
        DepthPool=DescriptorPoolBuilder().AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1).AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1).MaxSets(1).Create(device);
        DepthSet=DepthPool->allocate(DepthSetLayout.get());
        DepthSampler=SamplerBuilder().MinFilter(VK_FILTER_NEAREST).MagFilter(VK_FILTER_NEAREST).Create(device);
        WriteDescriptors().AddCombinedImageSampler(DepthSet.get(),0,depth,DepthSampler.get(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).AddStorageImage(DepthSet.get(),1,SceneDepthView.get(),VK_IMAGE_LAYOUT_GENERAL).Execute(device);
        std::string shader="#version 450\n";if(samples!=VK_SAMPLE_COUNT_1_BIT)shader+="#define MULTISAMPLED\n";
        shader+=R"(
            layout(local_size_x=8,local_size_y=8)in;
            #ifdef MULTISAMPLED
            layout(set=0,binding=0)uniform sampler2DMS sourceDepth;
            #else
            layout(set=0,binding=0)uniform sampler2D sourceDepth;
            #endif
            layout(r32f,set=0,binding=1)uniform writeonly image2D resolvedDepth;
            void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(resolvedDepth))))return;
                #ifdef MULTISAMPLED
                float d=1.0;for(int sampleIndex=0;sampleIndex<textureSamples(sourceDepth);++sampleIndex)d=min(d,texelFetch(sourceDepth,p,sampleIndex).r);
                #else
                float d=texelFetch(sourceDepth,p,0).r;
                #endif
                imageStore(resolvedDepth,p,vec4(d));
            }
        )";
        auto code=GLSLCompiler().Type(ShaderType::Compute).AddSource("CrestDepthResolve",shader).Compile(device);
        DepthPipeline=ComputePipelineBuilder().Layout(DepthLayout.get()).ComputeShader(code).Create(device);
    }
    void InitializeCapture(VulkanCommandBuffer* cmd)
    {
        if(Captured)return;
        PipelineBarrier().AddImage(Background.get(),VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT)
            .AddImage(SceneDepth.get(),VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT).Execute(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkClearColorValue color{};
        cmd->clearColorImage(Background->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
        color.float32[0]=1;cmd->clearColorImage(SceneDepth->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
        PipelineBarrier().AddImage(Background.get(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)
            .AddImage(SceneDepth.get(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT).Execute(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        Captured=true;
    }
    void Capture(VulkanCommandBuffer* cmd,VulkanImage* color,VulkanImage* depth,VkSampleCountFlagBits samples)
    {
        auto old=Captured?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        PipelineBarrier().AddImage(color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT)
            .AddImage(Background.get(),old,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,Captured?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT)
            .AddImage(depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_DEPTH_BIT)
            .AddImage(SceneDepth.get(),old,VK_IMAGE_LAYOUT_GENERAL,Captured?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_SHADER_WRITE_BIT)
            .Execute(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        const auto width=Background->width,height=Background->height;
        if(samples==VK_SAMPLE_COUNT_1_BIT){VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={uint32_t(width),uint32_t(height),1};cmd->copyImage(color->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Background->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);}
        else {VkImageResolve resolve{};resolve.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.dstSubresource=resolve.srcSubresource;resolve.extent={uint32_t(width),uint32_t(height),1};cmd->resolveImage(color->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Background->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&resolve);}
        cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,DepthPipeline.get());cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,DepthLayout.get(),0,DepthSet.get());cmd->dispatch((width+7)/8,(height+7)/8,1);
        PipelineBarrier().AddImage(color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
            .AddImage(Background.get(),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)
            .AddImage(depth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_IMAGE_ASPECT_DEPTH_BIT)
            .AddImage(SceneDepth.get(),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)
            .Execute(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        Captured=true;
    }
    struct Mesh { std::vector<vec3> Points;std::vector<float> Depth; };
    std::unordered_map<std::string,Mesh> Meshes;
    // Clip each authored BSP polygon against a fixed world grid. Shared edges
    // get the same wave samples, without covering dry land with an ocean plane.
    const Mesh& Tessellate(const SurfaceFacet& facet,const vec3& camera)
    {
        const int cx=int(std::floor(camera.x/512.f))*512,cy=int(std::floor(camera.y/512.f))*512;
        std::string key(reinterpret_cast<const char*>(facet.Vertices),facet.VertexCount*sizeof(vec3));
        if(facet.WaterDepth)key.append(reinterpret_cast<const char*>(facet.WaterDepth),facet.VertexCount*sizeof(float));
        key.append(reinterpret_cast<const char*>(&cx),sizeof(cx));key.append(reinterpret_cast<const char*>(&cy),sizeof(cy));
        auto found=Meshes.find(key);if(found!=Meshes.end())return found->second;
        Mesh mesh;
        struct Point {vec3 p;float d;};std::vector<Point> base;
        float minx=1e30f,miny=minx,maxx=-minx,maxy=-minx;
        for(int i=0;i<facet.VertexCount;++i){auto p=facet.Vertices[i];base.push_back({p,facet.WaterDepth?facet.WaterDepth[i]:512.f});minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);}
        if(Meshes.size()>2048)Meshes.clear();
        auto emit=[&](float x0,float x1,float y0,float y1) {
            if(x1<=x0 || y1<=y0)return;
            auto poly=base;
            for(int edge=0;edge<4 && poly.size()>=3;++edge) {
                int axis=edge/2;float limit=axis?(edge%2?y1:y0):(edge%2?x1:x0);float sign=edge%2?-1.f:1.f;
                std::vector<Point> next;auto previous=poly.back();float pd=(previous.p[axis]-limit)*sign;
                for(auto current:poly){float cd=(current.p[axis]-limit)*sign;if((pd>=0)!=(cd>=0)){float t=pd/(pd-cd);next.push_back({previous.p+(current.p-previous.p)*t,previous.d+(current.d-previous.d)*t});}if(cd>=0)next.push_back(current);previous=current;pd=cd;}poly=std::move(next);
            }
            for(size_t i=2;i<poly.size();++i){auto area=cross(poly[i-1].p-poly[0].p,poly[i].p-poly[0].p);if(dot(area,area)<1e-8f)continue;for(auto j:{size_t(0),i-1,i}){mesh.Points.push_back(poly[j].p);mesh.Depth.push_back(poly[j].d);}}
        };
        // Dense displaced patch near the viewer. The eight outer regions remain
        // coarse and meet the same zero-displacement boundary. This bounds cost
        // for enormous legacy BSP water polygons, without visible water holes.
        float xs[4]={minx,float(cx-2048),float(cx+2048),maxx},ys[4]={miny,float(cy-2048),float(cy+2048),maxy};
        xs[1]=std::clamp(xs[1],minx,maxx);xs[2]=std::clamp(xs[2],minx,maxx);ys[1]=std::clamp(ys[1],miny,maxy);ys[2]=std::clamp(ys[2],miny,maxy);
        for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
            if(x!=1||y!=1){emit(xs[x],xs[x+1],ys[y],ys[y+1]);continue;}
            for(int iy=int(std::floor(ys[y]/64));iy<int(std::ceil(ys[y+1]/64));++iy)for(int ix=int(std::floor(xs[x]/64));ix<int(std::ceil(xs[x+1]/64));++ix)
                emit(std::max(xs[x],float(ix*64)),std::min(xs[x+1],float(ix*64+64)),std::max(ys[y],float(iy*64)),std::min(ys[y+1],float(iy*64+64)));
        }
        double originalArea=0,meshArea=0;
        for(int i=0;i<facet.VertexCount;++i){const auto& a=facet.Vertices[i];const auto& b=facet.Vertices[(i+1)%facet.VertexCount];originalArea+=double(a.x)*b.y-double(a.y)*b.x;}
        for(size_t i=0;i<mesh.Points.size();i+=3){const auto& a=mesh.Points[i];const auto& b=mesh.Points[i+1];const auto& c=mesh.Points[i+2];meshArea+=(double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x);}
        if(std::abs(originalArea-meshArea)>std::max(std::abs(originalArea)*.0002,.1))throw std::runtime_error("Crest tessellation changed the authored water coverage");
        if(mesh.Points.size()>100000)throw std::runtime_error("Crest bounded water mesh exceeded its vertex budget");
        return Meshes.emplace(std::move(key),std::move(mesh)).first->second;
    }
};
