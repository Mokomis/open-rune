// Included by VulkanRenderDevice.cpp. One focused shadow map for a nearby light.
// Camera-independent caster visibility is supplied by VisibleFrame::ProcessShadow.
void VulkanRenderDevice::CreateShadowImage()
{
    ShadowImage = ImageBuilder().Size(ShadowSize*RuneShadows::Columns, ShadowSize*RuneShadows::Rows).Format(VK_FORMAT_D32_SFLOAT)
        .Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .DebugName("RemasteredShadowDepth").Create(Device.get());
    ShadowView = ImageViewBuilder().Image(ShadowImage.get(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT)
        .DebugName("RemasteredShadowView").Create(Device.get());
    // Manual PCF works even on devices without linear filtering of D32.
    ShadowSampler = SamplerBuilder().MinFilter(VK_FILTER_NEAREST).MagFilter(VK_FILTER_NEAREST)
        .MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST).MaxLod(0)
        .AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).DebugName("ShadowSampler").Create(Device.get());
}

void VulkanRenderDevice::CreateShadowPass()
{
    auto create = [&](bool load) {
        return RenderPassBuilder().AddDepthStencilAttachment(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
            load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .AddSubpass().AddSubpassDepthStencilAttachmentRef(0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .DebugName("RemasteredShadowPass").Create(Device.get());
    };
    ShadowPass = create(false); ShadowPassContinue = create(true);
    ShadowFramebuffer = FramebufferBuilder().RenderPass(ShadowPass.get()).Size(ShadowSize*RuneShadows::Columns, ShadowSize*RuneShadows::Rows)
        .AddAttachment(ShadowView.get()).DebugName("ShadowFramebuffer").Create(Device.get());
    ShadowPipeline.MinDepth = 0.0f;
    ShadowPipeline.Pipeline = GraphicsPipelineBuilder()
        .AddVertexShader(Shaders->ShadowVertex).AddFragmentShader(Shaders->ShadowFragment)
        .AddVertexBufferBinding(0, sizeof(SceneVertex))
        .AddVertexAttribute(0, 0, VK_FORMAT_R32_UINT, offsetof(SceneVertex, Flags))
        .AddVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, Position))
        .AddVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, TexCoord))
        .AddVertexAttribute(7, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(SceneVertex, TextureBinds))
        .Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST).Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
        .Viewport(0, 0, ShadowSize, ShadowSize).Scissor(0, 0, ShadowSize*RuneShadows::Columns, ShadowSize*RuneShadows::Rows)
        .AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT).AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS)
        .DepthStencilEnable(true, true, false).DepthBias(true, 1.25f, 0.0f, 1.75f)
        .Layout(RenderPasses->Scene.BindlessPipelineLayout.get()).RenderPass(ShadowPass.get())
        .DebugName("RemasteredShadowPipeline").Create(Device.get());
}

void VulkanRenderDevice::BeginShadowPass(bool continuation)
{
    auto cmd = Commands->GetDrawCommands();
    PipelineBarrier().AddImage(ShadowImage.get(), continuation ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        continuation ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        .Execute(cmd, continuation ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    RenderPassBegin().RenderPass(continuation || ShadowSlot>0 ? ShadowPassContinue.get() : ShadowPass.get())
        .Framebuffer(ShadowFramebuffer.get()).RenderArea(0, 0, ShadowSize*RuneShadows::Columns, ShadowSize*RuneShadows::Rows).AddClearDepthStencil(1, 0).Execute(cmd);
    VkBuffer vb[] = { Buffers->SceneVertexBuffer->buffer }; VkDeviceSize offsets[] = {0};
    cmd->bindVertexBuffers(0, 1, vb, offsets);
    cmd->bindIndexBuffer(Buffers->SceneIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    cmd->setViewport(0, 1, &viewportdesc);
}

bool VulkanRenderDevice::BeginLightShadow(SceneNode* frame, const vec3& light, const vec3& camera, float radius, int slot)
{
    if (!(WorldFeatures & 512) || DrawingShadow || radius < 2.0f || slot < 0 || slot >= RuneShadows::Capacity) return false;
    DrawBatch(Commands->GetDrawCommands());
    if (!ScenePassDeferred && !BetweenShadows) Commands->GetDrawCommands()->endRenderPass();
    const bool reusePass=BetweenShadows && GroupShadowPasses;
    DrawingShadow = true; BetweenShadows = false; ShadowSlot = slot;
    Buffers->ShadowLighting->Shadows[slot].WorldToShadow = frame->Projection * frame->WorldToView;
    Buffers->ShadowLighting->Shadows[slot].LightPosition = vec4(light, 1.0f);
    Buffers->ShadowLighting->CameraPosition = vec4(camera, 1.0f);
    viewportdesc = {float((slot%RuneShadows::Columns)*ShadowSize), float((slot/RuneShadows::Columns)*ShadowSize), float(ShadowSize), float(ShadowSize), 0, 1};
    // All shadow lights write disjoint atlas tiles before any receiver samples
    // them. Keep the tile memory resident rather than storing/reloading the
    // complete atlas and transitioning it to sampling between every light.
    if(!reusePass) BeginShadowPass(false);
    SetSceneNode(frame);
    return true;
}

void VulkanRenderDevice::EndLightShadow(bool more)
{
    if (!DrawingShadow) return;
    auto cmd = Commands->GetDrawCommands();
    DrawBatch(cmd);
    if(more && GroupShadowPasses) {
        DrawingShadow=false;BetweenShadows=true;
        return;
    }
    cmd->endRenderPass();
    PipelineBarrier().AddImage(ShadowImage.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
        .Execute(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    DrawingShadow = false;
    BetweenShadows = more;
    if (!more && !ScenePassDeferred) ResumeScenePass();
}

void VulkanRenderDevice::DrawShadowPolygon(TextureInfo& info, const GouraudVertex* pts, int count, uint32_t flags)
{
    if (count < 3 || (flags & (PF_Invisible | PF_Translucent | PF_Modulated | PF_FakeBackdrop))) return;
    // Only camera coverage fading is ignored; genuinely transparent effects do
    // not become opaque blockers. The alpha-tested texture keeps foliage holes.
    if (pts[0].Opacity < 1.0f && !pts[0].CameraFade) return;
    SetPipeline(&ShadowPipeline);
    auto* texture = Textures->GetTexture(&info, (flags & PF_Masked) != 0);
    ivec4 binds = GetTextureIndexes(flags, texture);
    auto alloc = ReserveVertices(count, (count-2)*3);
    if (!alloc.vptr) return;
    for (int i=0; i<count; ++i)
    {
        auto& v = alloc.vptr[i]; v = {};
        v.Position = pts[i].Point;
        v.TexCoord = vec2(pts[i].UV.x * GetUMult(info), pts[i].UV.y * GetVMult(info));
        v.Flags = (flags & PF_Masked) ? 1 : 0; v.TextureBinds = binds;
    }
    for (int i=2; i<count; ++i)
    {
        *alloc.iptr++ = alloc.vpos; *alloc.iptr++ = alloc.vpos+i-1; *alloc.iptr++ = alloc.vpos+i;
    }
    UseVertices(count, (count-2)*3);
}

vec4 VulkanRenderDevice::GetMaterialResponse(TextureInfo* info)
{
    if (!info || !info->Texture) return vec4(0.10f, 20.0f, 0.0f, 0.0f);
    auto found = MaterialCache.find(info->CacheID);
    if (found != MaterialCache.end()) return found->second;
    std::string name = info->Texture->Name.ToString();
    if (info->Texture->package) name += " " + info->Texture->package->GetPackageName().ToString();
    for (char& c : name) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    // Conservative material presets, not authored PBR maps. Unknown skins stay
    // rough; never infer polished metal from albedo brightness alone.
    vec4 response(0.10f, 20.0f, 0.0f, 0.0f);
    if (has("metal") || has("steel") || has("iron") || has("axe") || has("sword") || has("hammer") || has("blade") || has("shield"))
        response = vec4(0.70f, 48.0f, 0.45f, 0.0f);
    else if (has("water") || has("wet")) response = vec4(0.5f, 64.0f, 0.0f, 0.0f);
    else if (has("wood")) response = vec4(0.06f, 12.0f, 0.0f, 0.0f);
    return MaterialCache.emplace(info->CacheID, response).first->second;
}
