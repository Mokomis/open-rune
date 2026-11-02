void VulkanRenderDevice::PrepareApproximateWater(float height,float time)
{
    // No offscreen image, scene traversal or draw submission for this mode.
    Buffers->ShadowLighting->WaterParameters=vec4(time,height,1,1);
}
// A separate planar view preserves the real scene below transparent water.
bool VulkanRenderDevice::ReuseWaterReflection(float height,float time,const vec3& camera)
{
    // At most 30 reflected scene updates per game second. Waves and main-view
    // geometry still update every frame. Teleports/plane changes invalidate.
    if((WorldFeatures&10240)!=10240 || !CachedWaterValid)return false;
    vec3 delta=camera-CachedWaterCamera;
    bool reuse=CachedWaterFeatures==WorldFeatures &&
        std::abs(height-CachedWaterHeight)<0.5f && time>=CachedWaterTime &&
        time-CachedWaterTime<1.0f/30.0f && dot(delta,delta)<64.0f*64.0f;
    if(!reuse)return false;
    Buffers->ShadowLighting->WaterWorldToClip=CachedWaterProjection;
    Buffers->ShadowLighting->WaterParameters=vec4(time,height,1,0);
    ++WaterReuseCount;
    return true;
}
void VulkanRenderDevice::CreateWaterImage()
{
    WaterImage=ImageBuilder().Size(WaterSize,WaterSize).Format(SceneColorFormat)
        .Usage(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT).DebugName("WaterReflection").Create(Device.get());
    WaterView=ImageViewBuilder().Image(WaterImage.get(),SceneColorFormat,VK_IMAGE_ASPECT_COLOR_BIT).Create(Device.get());
}
void VulkanRenderDevice::CreateWaterTargets()
{
    if(WaterFramebuffer && WaterSamples==Textures->Scene->SceneSamples) return;
    Commands->WaitForPending();
    WaterFramebuffer.reset();WaterColorView.reset();WaterDepthView.reset();WaterHitView.reset();
    WaterSamples=Textures->Scene->SceneSamples;
    auto make=[&](VkFormat format,VkImageUsageFlags usage) {return ImageBuilder().Size(WaterSize,WaterSize).Samples(WaterSamples).Format(format).Usage(usage).Create(Device.get());};
    WaterColor=make(SceneColorFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    WaterHit=make(VK_FORMAT_R32_UINT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    WaterDepth=make(VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    WaterColorView=ImageViewBuilder().Image(WaterColor.get(),SceneColorFormat,VK_IMAGE_ASPECT_COLOR_BIT).Create(Device.get());
    WaterHitView=ImageViewBuilder().Image(WaterHit.get(),VK_FORMAT_R32_UINT,VK_IMAGE_ASPECT_COLOR_BIT).Create(Device.get());
    WaterDepthView=ImageViewBuilder().Image(WaterDepth.get(),VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_DEPTH_BIT).Create(Device.get());
    FramebufferBuilder fb; fb.RenderPass(RenderPasses->Scene.RenderPassNoHit.get()).Size(WaterSize,WaterSize)
        .AddAttachment(WaterColorView.get()).AddAttachment(WaterHitView.get()).AddAttachment(WaterDepthView.get());
    if(UsesInPassResolve())fb.AddAttachment(WaterView.get());
    WaterFramebuffer=fb.DebugName("WaterReflectionFramebuffer").Create(Device.get());
}
void VulkanRenderDevice::BeginWaterPass(bool continuation)
{
    auto cmd=Commands->GetDrawCommands();
    PipelineBarrier barrier;
    auto colorAccess=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    auto depthAccess=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.AddImage(WaterColor.get(),continuation?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,colorAccess|VK_ACCESS_TRANSFER_READ_BIT,colorAccess)
        .AddImage(WaterHit.get(),continuation?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,colorAccess,colorAccess)
        .AddImage(WaterDepth.get(),continuation?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,depthAccess,depthAccess,VK_IMAGE_ASPECT_DEPTH_BIT);
    if(UsesInPassResolve())barrier.AddImage(WaterImage.get(),continuation?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT|colorAccess,colorAccess);
    barrier.Execute(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
    RenderPassBegin().RenderPass(RenderPasses->GetScenePass(continuation,false,true)).Framebuffer(WaterFramebuffer.get())
        .RenderArea(0,0,WaterSize,WaterSize).AddClearColor(0,0,0,1).AddClearColor(0,0,0,0).AddClearDepthStencil(1,0).Execute(cmd);
    VkBuffer vb[]={Buffers->SceneVertexBuffer->buffer}; VkDeviceSize offsets[]={0};
    cmd->bindVertexBuffers(0,1,vb,offsets);cmd->bindIndexBuffer(Buffers->SceneIndexBuffer->buffer,0,VK_INDEX_TYPE_UINT32);
    cmd->setViewport(0,1,&viewportdesc);
}
bool VulkanRenderDevice::BeginWaterReflection(SceneNode* frame,float height,float time)
{
    if((WorldFeatures&10240)!=10240||DrawingShadow||WaterReflectionActive)return false;
    DrawBatch(Commands->GetDrawCommands());
    if(!ScenePassDeferred)Commands->GetDrawCommands()->endRenderPass();
    CreateWaterTargets();WaterReflectionActive=true;
    Buffers->ShadowLighting->WaterWorldToClip=frame->Projection*frame->WorldToView;
    Buffers->ShadowLighting->WaterParameters=vec4(time,height,1,0);
    viewportdesc={0,0,float(WaterSize),float(WaterSize),0.1f,1};
    BeginWaterPass(false);SetSceneNode(frame);return true;
}
void VulkanRenderDevice::EndWaterReflection()
{
    if(!WaterReflectionActive)return;
    auto cmd=Commands->GetDrawCommands();DrawBatch(cmd);cmd->endRenderPass();
    if(!UsesInPassResolve()) {
        PipelineBarrier().AddImage(WaterColor.get(),VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT)
            .AddImage(WaterImage.get(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)
            .Execute(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        if(WaterSamples!=VK_SAMPLE_COUNT_1_BIT) {
            VkImageResolve resolve{};resolve.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.dstSubresource=resolve.srcSubresource;resolve.extent={WaterSize,WaterSize,1};
            cmd->resolveImage(WaterColor->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,WaterImage->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&resolve);
        } else {
            VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={WaterSize,WaterSize,1};
            cmd->copyImage(WaterColor->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,WaterImage->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        }
    }
    PipelineBarrier().AddImage(WaterImage.get(),UsesInPassResolve()?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        UsesInPassResolve()?VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)
        .Execute(cmd,UsesInPassResolve()?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CachedWaterProjection=Buffers->ShadowLighting->WaterWorldToClip;
    CachedWaterHeight=Buffers->ShadowLighting->WaterParameters.y;
    CachedWaterTime=Buffers->ShadowLighting->WaterParameters.x;
    CachedWaterCamera=Buffers->ShadowLighting->CameraPosition.xyz();
    CachedWaterFeatures=WorldFeatures;CachedWaterValid=true;++WaterRenderCount;
    if(commandline->HasArg("","--trace-rune-combat") && (WaterRenderCount==1 || WaterRenderCount%120==0))
        LogMessage("RUNE_WATER_BUDGET rendered="+std::to_string(WaterRenderCount)+" reused="+std::to_string(WaterReuseCount)+" size="+std::to_string(WaterSize)+" time="+std::to_string(CachedWaterTime));
    WaterReflectionActive=false;
    if(ScenePassDeferred)StartScenePass();else ResumeScenePass();
}
