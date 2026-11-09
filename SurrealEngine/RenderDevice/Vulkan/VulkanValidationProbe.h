#pragma once

// Opt-in GPU regression. Uses the production draw, continuation, hit-readback
// and screenshot paths at the current native resolution and sample count.
bool VulkanRenderDevice::Exec(std::string Cmd, OutputDevice& Ar)
{
    if(Cmd=="RuneModernWaterProbe"){RunModernWaterProbe();return true;}
    if(Cmd=="RuneMaterialLightingProbe"){RunMaterialLightingProbe();return true;}
    if(Cmd=="RuneShadowBudgetProbe"){RunShadowBudgetProbe();return true;}
    if(Cmd=="RuneTexturePackProbe")
    {
        Textures->ValidateReplacementUploads();return true;
    }
	if (Cmd != "RuneVulkanProbe") return false;
	RunValidationProbe();
	return true;
}

void VulkanRenderDevice::RunValidationProbe()
{
	if (IsLocked || GetSettingsMultisample() != 4 || ResolutionScale != 1.0f)
		throw std::runtime_error("Vulkan probe requires an unlocked native 4x renderer");
	const bool savedEnhanced = VisualEnhanced, savedBloom = Bloom;
	const bool savedGamma = GammaCorrectScreenshots;
	const bool savedResolve = InPassResolve;
	const float savedSharpness = AdaptiveSharpness, savedShadowLift = ShadowLift, savedAnisotropy = TextureAnisotropy;
	AdaptiveSharpness = ShadowLift = 0.0f;
	const int savedHitX = HitX, savedHitY = HitY, savedHitWidth = HitWidth, savedHitHeight = HitHeight;
	const int w = GetRenderWidth(), h = GetRenderHeight();
	SceneNode frame = {};
	frame.X = w; frame.Y = h; frame.FX = (float)w; frame.FY = (float)h;
	frame.FX2 = w * 0.5f; frame.FY2 = h * 0.5f; frame.FovAngle = 90.0f;
	frame.ObjectToWorld = frame.WorldToView = mat4::identity();
	Bloom = false;
	GammaCorrectScreenshots = false;
	std::vector<TextureColor> baseline((size_t)w * h), pixels(baseline.size());
	auto require = [](bool ok, const char* message)
	{
		if (!ok) throw std::runtime_error(std::string("Vulkan probe: ") + message);
	};
	auto rect = [&](float x0, float x1, float z, vec4 color, bool alpha, float coverage = -1.0f, bool invisible = false)
	{
		SetPipeline(RenderPasses->GetPipeline((alpha ? PF_Highlighted : PF_Occlude) | (invisible ? PF_Invisible : 0), false, coverage >= 0));
		auto binds = GetTextureIndexes(0, nullptr);
		auto alloc = ReserveVertices(4, 6);
		require(alloc.vptr != nullptr, "vertex allocation");
		const vec3 positions[] = {{x0,-0.8f,z},{x1,-0.8f,z},{x1,0.8f,z},{x0,0.8f,z}};
		for (int i = 0; i < 4; i++)
			alloc.vptr[i] = {coverage >= 0 ? 256u : 0u, positions[i], vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(coverage, 0.0f), color, binds};
		const uint32_t indices[] = {0,1,2,0,2,3};
		for (int i = 0; i < 6; i++) alloc.iptr[i] = alloc.vpos + indices[i];
		UseVertices(4, 6);
	};
	for (int enhanced = 0; enhanced < 2; enhanced++)
	{
		VisualEnhanced = enhanced != 0;
		for (int resolveCase = 0; resolveCase < (savedResolve ? 2 : 1); resolveCase++)
		{
			InPassResolve = resolveCase != 0;
			// Recreate the same native 4x targets to test compatibility/lifetime
			// and compare in-pass resolve directly with external resolve pixels.
			Commands->WaitForPending();
			Framebuffers->DestroySceneFramebuffer();
			Textures->Scene.reset();
			for (int split = 0; split < 3; split++)
			{
				// Test normal frames, then hits before/after continuation and a miss.
				for (int hitCase = 0; hitCase < 4; hitCase++)
				{
					uint8_t hitResult = 0; int hitSize = 0;
					HitX = hitCase == 1 ? w / 4 : hitCase == 2 ? w * 3 / 4 : 2;
					HitY = hitCase == 3 ? 2 : h / 2; HitWidth = HitHeight = 4;
					Lock(vec4(0.5f), vec4(0.0f), vec4(0.0f), hitCase ? &hitResult : nullptr, hitCase ? &hitSize : nullptr);
					require(Textures->Scene->SceneSamples == VK_SAMPLE_COUNT_4_BIT, "actual sample count");
					SetSceneNode(&frame);
					pushconstants.objectToProjection = mat4::identity();
					pushconstants.objectToView = mat4::identity();
					pushconstants.nearClip = vec4(0,0,0,1);
					uint8_t token = 7;
					PushHit(&token, 1);
					rect(-0.8f, 0.0f, 0.2f, vec4(1,0,0,1), false);
					PopHit(1, false);
					if (split == 1) FlushDrawBatchAndWait();
					if (split == 2) Flush(false);
					token = 9; PushHit(&token, 1);
					rect(-0.8f, 0.0f, 0.7f, vec4(0,0,1,1), false); // Must remain occluded.
					PopHit(1, false);
					token = 11; PushHit(&token, 1);
					rect(0.0f, 0.8f, 0.4f, vec4(0,1,0,1), false);
					PopHit(1, false);
					rect(-0.2f, 0.2f, 0.1f, vec4(0,0,0.5f,0.5f), true);
					Unlock(false);
					if (hitCase)
						require(hitCase == 3 ? hitSize == 0 : hitSize == 1 && hitResult == (hitCase == 1 ? 7 : 11), "hit rectangle/result");
					ReadPixels(pixels.data());
					if (!enhanced)
					{
						const auto red = pixels[(size_t)(h / 2) * w + w / 4];
						const auto green = pixels[(size_t)(h / 2) * w + w * 3 / 4];
						const auto blend = pixels[(size_t)(h / 2) * w + w * 9 / 20];
						require(red.R > 250 && red.G < 2 && red.B < 2, "preserved color/depth before continuation");
						require(green.G > 250 && green.R < 2 && green.B < 2, "draw after continuation");
						require(blend.R > 120 && blend.R < 135 && blend.B > 120 && blend.B < 135, "translucent overlap");
					}
					if (!resolveCase && !split && !hitCase) baseline = pixels;
					else
					{
						for (size_t i = 0; i < pixels.size(); i++)
							require(std::abs(int(pixels[i].R) - baseline[i].R) <= 1 && std::abs(int(pixels[i].G) - baseline[i].G) <= 1 && std::abs(int(pixels[i].B) - baseline[i].B) <= 1, "continuation/capture pixel mismatch");
					}
				}
			}
		}
	}
	VisualEnhanced = false;
	for (int order = 0; order < 2; order++)
		for (int step = 0; step <= 4; step++)
		{
			float coverage = step * 0.25f;
			Lock(vec4(0.5f), vec4(0.0f), vec4(0.0f), nullptr, nullptr);
			SetSceneNode(&frame);
			pushconstants.objectToProjection = mat4::identity();
			pushconstants.objectToView = mat4::identity();
			pushconstants.nearClip = vec4(0,0,0,1);
			rect(-0.8f, 0.8f, 0.8f, vec4(0,1,0,1), false);
			for (int surface = 0; surface < 2; surface++)
			{
				bool nearValue = (surface == order);
				rect(-0.8f, 0.8f, nearValue ? 0.2f : 0.5f, nearValue ? vec4(1,0,0,1) : vec4(0,0,1,1), false, coverage);
			}
			Unlock(false);
			ReadPixels(pixels.data());
			int covered = 0, total = 0;
			for (int y = h/3; y < h*2/3; y++)
				for (int x = w/3; x < w*2/3; x++)
				{
					auto c = pixels[(size_t)y*w+x];
					require(c.B < 2, "camera fade shows hidden rear surface");
					require((c.R > 250 && c.G < 2) || (c.G > 250 && c.R < 2), "camera fade must retain nearest surface or background");
					covered += c.R > 250; total++;
				}
			require(std::abs(float(covered)/total - coverage) < 0.01f, "camera fade coverage fraction");
		}
    // A fading invisible surface must still occlude without writing color.
    // This catches accidentally routing all coverage draws to one opaque state.
    Lock(vec4(0.5f),vec4(0),vec4(0),nullptr,nullptr);
    SetSceneNode(&frame);
    pushconstants.objectToProjection=pushconstants.objectToView=mat4::identity();
    pushconstants.nearClip=vec4(0,0,0,1);
    rect(-0.8f,0.8f,0.8f,vec4(0,1,0,1),false);
    rect(-0.8f,0.8f,0.2f,vec4(1,0,0,1),false,1.0f,true);
    rect(-0.8f,0.8f,0.5f,vec4(0,0,1,1),false);
    Unlock(false);ReadPixels(pixels.data());
    const auto invisibleCoverage=pixels[(size_t)(h/2)*w+w/2];
    require(invisibleCoverage.R<2 && invisibleCoverage.G>250 && invisibleCoverage.B<2,
        "invisible camera coverage must retain color and write depth");
	LogMessage("RUNE_CAMERA_COVERAGE_OK native=1 samples=4 fractions=5 draw_orders=2 hidden_surface=0 background_visible=1 invisible_depth=1");
	// A small point light inside a coarse quad cannot be represented by the
	// four corner lighting values. Check the production mesh upload and fragment
	// path, as well as original/unlit/two-sided/ambient behavior and continuation.
	const int savedWorld = WorldFeatures, savedLightMode = LightMode;
	LightMode = 0;
	UnrealMipmap whiteMip; whiteMip.Width = whiteMip.Height = 1;
	whiteMip.Data.resize(4); for (auto& b : whiteMip.Data) b = 255;
	TextureInfo whiteInfo; whiteInfo.CacheID = 0xfedcba9876543210ull;
	whiteInfo.Format = TextureFormat::BGRA8; whiteInfo.NumMips = 1; whiteInfo.Mips = &whiteMip;
	for (int split = 0; split < 3; split++)
	for (int mode = 0; mode < 8; mode++)
	{
		WorldFeatures = mode == 0 || mode == 6 ? 0 : 256;
		VertexLight lighting{};
		lighting.AmbientColor = vec3(0.02f); lighting.ScaleGlow = 1.0f;
		lighting.NumLights = mode == 5 ? 0 : mode == 7 ? 8 : 1;
		for (int i = 0; i < lighting.NumLights; i++)
		{
			lighting.Lights[i].Location = vec3(0,0,0.6f);
			lighting.Lights[i].Color = vec3(0.2f / lighting.NumLights);
			lighting.Lights[i].InvRadiusSquared = 1.0f;
		}
		GouraudVertex verts[4];
		const vec3 positions[] = {{-0.8f,-0.8f,0.2f},{0.8f,-0.8f,0.2f},{0.8f,0.8f,0.2f},{-0.8f,0.8f,0.2f}};
		uint32_t polyflags = PF_Occlude | (mode == 3 ? PF_TwoSided : 0) | (mode == 4 ? PF_Unlit : 0);
		for (int i = 0; i < 4; i++)
		{
			verts[i].Point = positions[i]; verts[i].Normal = vec3(0,0, mode == 2 || mode == 3 ? -1 : 1);
			verts[i].Lighting = &lighting; verts[i].UV = vec2(0.5f); verts[i].Fog = vec4(0);
			verts[i].Light = lighting.GetVertexLight(positions[i],verts[i].Normal,mode == 4,mode == 3);
		}
		Lock(vec4(0.5f),vec4(0),vec4(0),nullptr,nullptr);
		SetSceneNode(&frame);
		pushconstants.objectToProjection = pushconstants.objectToView = mat4::identity();
		pushconstants.nearClip = vec4(0,0,0,1);
		DrawGouraudPolygon(&frame,whiteInfo,verts,4,polyflags);
		if (split == 1) FlushDrawBatchAndWait();
		if (split == 2) Flush(false);
		if (split) DrawGouraudPolygon(&frame,whiteInfo,verts,4,polyflags);
		Unlock(false); ReadPixels(pixels.data());
		int center = pixels[(size_t)(h/2)*w+w/2].R;
		int edge = pixels[(size_t)(h/2)*w+w*85/100].R;
		if (mode == 1 || mode == 3 || mode == 7)
			require(center > 160 && center < 175 && edge < 80, "mesh per-pixel point light / normal / eight lights");
		else if (mode == 4) require(center > 250, "unlit mesh bypass");
		else require(center >= 14 && center <= 17, "original / reverse normal / ambient / off round trip");
	}
    // Production BSP path: a sub-lightmap point light, original shadow masks,
    // retained base illumination, eight lights and both continuation paths.
    for (int split = 0; split < 3; split++)
    for (int mode = 0; mode < 15; mode++)
    {
        WorldFeatures = mode == 0 || mode == 5 ? 0 : 256;
        if(mode>=6 && mode!=10) WorldFeatures=768;
        SurfaceLight light{};
        light.Count = mode == 3 ? 8 : (mode == 4 || mode>=6) ? 0 : 1;
        light.Origin = vec3(-1,-1,0.2f); light.U = vec3(2,0,0); light.V = vec3(0,2,0); light.Normal = vec3(0,0,1);
        if(mode==14)light.Origin.x+=100.0f;
        for (int i = 0; i < light.Count; i++)
        {
            light.Lights[i] = {vec3(0,0,0.6f),vec3(0.2f/light.Count),1.0f,0.0f};
        }
        for (auto& shadow : light.Shadows)
        {
            shadow.Width = shadow.Height = 1; shadow.Data.resize(sizeof(vec4));
            vec4 visibility(mode == 2 ? 0.0f : 1.0f);
            std::memcpy(shadow.Data.data(), &visibility, sizeof(visibility));
        }
        UnrealMipmap mip; mip.Width = mip.Height = 1; mip.Data.resize(sizeof(vec4));
        vec4 baseLight(mode==13 ? 0.45f : mode == 4 || mode>=6 ? 0.15f : 0.02f);
        std::memcpy(mip.Data.data(), &baseLight, sizeof(baseLight));
        TextureInfo map; map.CacheID = 0xfedcba9876543305ull; map.Format = TextureFormat::RGBA32_F;
        map.Mips = &mip; map.NumMips = 1; map.UScale = map.VScale = 2; map.bRealtimeChanged = true; map.PixelLighting = &light;
        SurfaceInfo surface; surface.Texture = &whiteInfo; surface.LightMap = &map; surface.PolyFlags = PF_Occlude;
        vec3 positions[] = {{-0.8f,-0.8f,0.2f},{0.8f,-0.8f,0.2f},{0.8f,0.8f,0.2f},{-0.8f,0.8f,0.2f}};
        SurfaceFacet facet{}; facet.Vertices = positions; facet.VertexCount = 4;
        facet.MapCoords.Origin = vec3(0,0,0.2f); facet.MapCoords.XAxis = vec3(1,0,0); facet.MapCoords.YAxis = vec3(0,1,0);
        Lock(vec4(0.5f),vec4(0),vec4(0),nullptr,nullptr); SetSceneNode(&frame);
        if(mode>=6) SetContactShadow(0,vec4(mode==14?100.0f:mode==11?1.5f:0.0f,0,mode==8?3.2f:0.2f,4.0f),
            vec4(mode==9?vec3(1,0,0):vec3(0,0,1),(mode==7 || mode==13)?0.0f:mode==12?0.24f:0.48f));
        pushconstants.objectToProjection = pushconstants.objectToView = mat4::identity(); pushconstants.nearClip = vec4(0,0,0,1);
        if(mode==14)frame.ObjectToWorld=mat4::translate(vec3(100,0,0));
        DrawComplexSurface(&frame,surface,facet);
        if (split == 1) FlushDrawBatchAndWait();
        if (split == 2) Flush(false);
        if (split) DrawComplexSurface(&frame,surface,facet);
        frame.ObjectToWorld=mat4::identity();
        Unlock(false); ReadPixels(pixels.data());
        int center = pixels[(size_t)(h/2)*w+w/2].R;
        int edge = pixels[(size_t)(h/2)*w+w*85/100].R;
        if(mode>=6) {
            LogMessage("RUNE_CONTACT_PIXELS mode="+std::to_string(mode)+" split="+std::to_string(split)+" center="+std::to_string(center)+" edge="+std::to_string(edge));
            if(mode==13) require(center>=227 && center<=232,"contact-only surface retains bright baked light");
            else if(mode==6 || mode==14) require(center>=37 && center<=42,"contact shadow darkens ground including transformed brush");
            else if(mode==12) require(center>=55 && center<=61,"contact shadow height fade");
            else if(mode==10) require(center>73 && center<80,"contact shadow original mode gate");
            else require(center>=73 && center<=80,"contact shadow plane/normal/radius/disabled guard");
            require(edge>=(mode==13?227:73) && edge<=(mode==13?232:80),"contact shadow preserves distant surface");
        }
        else if (mode == 1 || mode == 3) require(center > 109 && center < 115 && edge < 55,"world per-pixel falloff/eight lights");
        else if (mode == 4) require(center > 73 && center < 80,"world retained base illumination");
        else require(center >= 9 && center <= 12,"world shadow preservation/original round-trip");
    }
    LogMessage("RUNE_PIXEL_WORLD_OK native=1 samples=4 point_light=1 shadows=1 base_light=1 eight_lights=1 original=1 continuation=2");
    LogMessage("RUNE_CONTACT_SHADOW_OK ground=1 plane_guard=1 normal_guard=1 radius_guard=1 fade=1 mode_gate=1 continuation=2");
    // Actual depth pass -> mesh/BSP receivers. Moving and masked casters must
    // change the GPU pixels, including a buffer continuation inside the pass.
    const bool savedGroupShadows=GroupShadowPasses, savedReceiverCull=CullShadowReceivers;
    std::vector<TextureColor> receiverCullReference;
    for (int worldReceiver=0; worldReceiver<2; ++worldReceiver)
    for (int shadowCase=0; shadowCase<(worldReceiver?9:8); ++shadowCase)
    for (int receiverCull=0;receiverCull<(worldReceiver?2:1);++receiverCull)
    for (int grouped=0;grouped<2;++grouped)
    {
        GroupShadowPasses=grouped!=0;
        CullShadowReceivers=receiverCull!=0;
        WorldFeatures = shadowCase==4 ? 256 : 768;
        SceneNode lightFrame=frame;
        lightFrame.X=lightFrame.Y=ShadowSize; lightFrame.FX=lightFrame.FY=float(ShadowSize);
        lightFrame.Projection=mat4::scale(0.01f);
        lightFrame.WorldToView=mat4::identity();
        VertexLight lighting{}; lighting.AmbientColor=vec3(0.02f); lighting.ScaleGlow=1;
        lighting.NumLights=1;
        lighting.Lights[0].Location=vec3(0,0,-20); lighting.Lights[0].Color=vec3(0.3f);
        lighting.Lights[0].InvRadiusSquared=1.0f/(250.0f*250.0f);
        GouraudVertex receiver[4],caster[4];
        vec3 points[4]={{-80,-80,60},{80,-80,60},{80,80,60},{-80,80,60}};
        float x0=(shadowCase==1 || shadowCase==7) ? 10 : -75, x1=(shadowCase==1 || shadowCase==7) ? 75 : -10;
        vec3 casterPoints[4]={{x0,-80,20},{x1,-80,20},{x1,80,20},{x0,80,20}};
        for(int i=0;i<4;++i)
        {
            receiver[i].Point=points[i]; receiver[i].Normal=vec3(0,0,-1); receiver[i].Lighting=&lighting;
            receiver[i].Light=vec3(0.1f); receiver[i].UV=vec2(0.5f); receiver[i].Fog=vec4(0);
            caster[i]=receiver[i]; caster[i].Point=casterPoints[i];
        }
        UnrealMipmap maskMip=whiteMip; maskMip.Data[3]=0;
        TextureInfo maskInfo=whiteInfo; maskInfo.CacheID=0xfedcba9876544400ull; maskInfo.Mips=&maskMip;
        Lock(vec4(0.5f),vec4(0),vec4(0),nullptr,nullptr);
        SetLightingCamera(vec3(0,0,-100));
        if(shadowCase==5)
        {
            SetSceneNode(&frame); pushconstants.objectToProjection=mat4::identity();
            pushconstants.objectToView=mat4::identity(); pushconstants.nearClip=vec4(0,0,0,1);
            rect(-0.8f,0.8f,0.9f,vec4(0.02f),false);
        }
        if(shadowCase>=6) for(int slot=0;slot<2;++slot) {
            require(BeginLightShadow(&lightFrame,vec3(100+slot*20,0,-20),vec3(0,0,-100),250,slot),"extra shadow slot");
            DrawGouraudPolygon(&lightFrame,maskInfo,caster,4,PF_Occlude|PF_Masked);EndLightShadow(true);
        }
        bool began=BeginLightShadow(&lightFrame,lighting.Lights[0].Location,vec3(0,0,-100),250,shadowCase>=6?2:0);
        require(began==(shadowCase!=4),"shadow mode gate");
        if(began)
        {
            DrawGouraudPolygon(&lightFrame,shadowCase==2?maskInfo:whiteInfo,caster,4,PF_Occlude|(shadowCase==2?PF_Masked:0));
            if(shadowCase==3) FlushDrawBatchAndWait();
            EndLightShadow();
        }
        SetSceneNode(&frame);
        pushconstants.objectToProjection=mat4::scale(0.01f); pushconstants.objectToView=mat4::identity();
        pushconstants.nearClip=vec4(0,0,0,1);
        if(shadowCase==8) {
            for(auto& point:points)point.x+=200;
            pushconstants.objectToProjection=mat4::scale(.01f)*mat4::translate(-200,0,0);
        }
        if(!worldReceiver) DrawGouraudPolygon(&frame,whiteInfo,receiver,4,PF_Occlude);
        else
        {
            SurfaceLight light{}; light.Count=1;
            light.Origin=vec3(-100,-100,60); light.U=vec3(200,0,0); light.V=vec3(0,200,0); light.Normal=vec3(0,0,-1);
            light.Lights[0]={vec3(0,0,-20),vec3(0.3f),1.0f/(250.0f*250.0f),0};
            for(auto& mask:light.Shadows)
            {
                mask.Width=mask.Height=1; mask.Data.resize(sizeof(vec4)); vec4 visible(1);
                std::memcpy(mask.Data.data(),&visible,sizeof(visible));
            }
            UnrealMipmap mip; mip.Width=mip.Height=1; mip.Data.resize(sizeof(vec4)); vec4 base(0.04f);
            std::memcpy(mip.Data.data(),&base,sizeof(base));
            TextureInfo map; map.CacheID=0xfedcba9876544505ull; map.Format=TextureFormat::RGBA32_F;
            map.Mips=&mip; map.NumMips=1; map.UScale=map.VScale=200; map.bRealtimeChanged=true; map.PixelLighting=&light;
            SurfaceInfo surface; surface.Texture=&whiteInfo; surface.LightMap=&map; surface.PolyFlags=PF_Occlude;
            SurfaceFacet facet{}; facet.Vertices=points; facet.VertexCount=4;
            facet.MapCoords.Origin=vec3(0,0,60); facet.MapCoords.XAxis=vec3(1,0,0); facet.MapCoords.YAxis=vec3(0,1,0);
            DrawComplexSurface(&frame,surface,facet);
            if(shadowCase==8) require((Buffers->MeshLights[Buffers->MeshLightCount-1].Meta.w==0)==CullShadowReceivers,
                "outside receiver skips only the dynamic shadow lookup");
        }
        Unlock(false); ReadPixels(pixels.data());
        if(worldReceiver && !grouped) {
            if(!receiverCull) receiverCullReference=pixels;
            else for(size_t i=0;i<pixels.size();++i)
                require(pixels[i].R==receiverCullReference[i].R && pixels[i].G==receiverCullReference[i].G &&
                    pixels[i].B==receiverCullReference[i].B,"conservative receiver culling preserves shadow pixels");
        }
        if(!grouped)baseline=pixels;
        else for(size_t i=0;i<pixels.size();++i)
            require(std::abs(int(pixels[i].R)-baseline[i].R)<=1 && std::abs(int(pixels[i].G)-baseline[i].G)<=1 &&
                std::abs(int(pixels[i].B)-baseline[i].B)<=1,"grouped shadow pixels differ");
        int left=pixels[(size_t)(h/2)*w+w/4].R,right=pixels[(size_t)(h/2)*w+w*3/4].R;
        LogMessage("RUNE_SHADOW_PIXELS world="+std::to_string(worldReceiver)+" case="+std::to_string(shadowCase)+" left="+std::to_string(left)+" right="+std::to_string(right));
        if(shadowCase==0 || shadowCase==3 || shadowCase==5 || shadowCase==6) require(left+40<right,"cast shadow and shadow continuation");
        if(shadowCase==1 || shadowCase==7) require(right+40<left,"moving caster moves shadow");
        if(shadowCase==2 || shadowCase==4) require(std::abs(left-right)<=2 && left>90,"masked holes and disabled shadow mode");
    }
    GroupShadowPasses=savedGroupShadows;
    CullShadowReceivers=savedReceiverCull;
    LogMessage("RUNE_SHADOW_RECEIVERS_GPU_OK outside_culled=1 reference_pixel_match=1 continuation=1");
    LogMessage("RUNE_DYNAMIC_SHADOW_OK native=1 samples=4 moving_caster=1 masked=1 mesh_receiver=1 world_receiver=1 continuation=1 mode_gate=1 grouped_pixel_match=1");

    int materialBaseline=0;
    std::vector<TextureColor> basisReference;
    for(int mapped=0;mapped<5;++mapped) {
        WorldFeatures=768|1024;
        Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
        SetLightingCamera(vec3(0,0,-100));SetSceneNode(&frame);
        pushconstants.objectToProjection=mat4::scale(.01f);pushconstants.nearClip=vec4(0,0,0,1);
        VertexLight lighting{};lighting.NumLights=1;lighting.ScaleGlow=1;lighting.AmbientColor=vec3(.01f);
        lighting.Lights[0].Location=vec3(-80,0,-20);lighting.Lights[0].Color=vec3(.12f);lighting.Lights[0].InvRadiusSquared=1.0f/(250*250);
        GouraudVertex verts[4];vec3 points[]={{-80,-80,60},{80,-80,60},{80,80,60},{-80,80,60}};
        for(int i=0;i<4;++i){verts[i].Point=points[i];verts[i].Normal=vec3(0,0,-1);verts[i].Lighting=&lighting;verts[i].Light=vec3(.1f);verts[i].Fog=vec4(0);verts[i].UV=vec2(points[i].x/160+.5f,points[i].y/160+.5f);}
        if(mapped>=3)for(auto& vertex:verts)vertex.UV.x=1.0f-vertex.UV.x;
        UnrealMipmap packed=whiteMip;packed.Data[0]=225;packed.Data[1]=128;packed.Data[2]=210;packed.Data[3]=180;
        TextureInfo map=whiteInfo;map.CacheID=0xfedcba9876544600ull;map.Mips=&packed;
        int index=DescriptorSets->GetTextureArrayIndex(0,Textures->GetTexture(&map,false));
        DrawGouraudPolygon(&frame,whiteInfo,verts,4,PF_Occlude);
        if(mapped)Buffers->MeshLights[Buffers->MeshLightCount-1].MaterialMaps=ivec4(index,1,0,0);
        if(mapped==2 || mapped==4) {
            auto& packet=Buffers->MeshLights[Buffers->MeshLightCount-1];
            packet.SurfaceTangent=vec4(mapped==4?-1.0f:1.0f,0,0,0);
            packet.SurfaceBitangent=vec4(0,1,0,1);
        }
        Unlock(false);ReadPixels(pixels.data());
        if(mapped==1 || mapped==3)basisReference=pixels;
        if(mapped==2 || mapped==4)for(size_t i=0;i<pixels.size();++i)
            require(std::abs(int(pixels[i].R)-basisReference[i].R)<=2 &&
                std::abs(int(pixels[i].G)-basisReference[i].G)<=2 &&
                std::abs(int(pixels[i].B)-basisReference[i].B)<=2,"planar normal basis matches derivative basis including mirrored UVs");
        int value=pixels[(size_t)(h/2)*w+w/2].R;
        LogMessage("RUNE_NORMAL_MAP_PIXEL mode="+std::to_string(mapped)+" value="+std::to_string(value));
        if(!mapped)materialBaseline=value;else if(mapped<3)require(std::abs(value-materialBaseline)>10,"normal and roughness affect actual pixel lighting");
    }
    LogMessage("RUNE_MATERIAL_MAPS_GPU_OK native=1 samples=4 tangent_normal=1 roughness=1 planar_basis=1 mirrored_basis=1");
    RunMaterialLightingProbe();
    // Offscreen reflection, clipping, continuation and transparent underwater
    // visibility use the production 4x pass and production water material.
    for(int split=0;split<3;++split) {
        WorldFeatures=10240;
        Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
        SetLightingCamera(vec3(0,0,10));
        SceneNode reflected=frame; reflected.Projection=mat4::identity();
        reflected.NearClip=vec4(1,0,0,0);
        require(BeginWaterReflection(&reflected,.2f,1.25f),"water pass gate");
        pushconstants.objectToProjection=mat4::identity();
        rect(-.8f,.8f,.4f,vec4(1,0,0,1),false);
        if(split) {FlushDrawBatchAndWait(); pushconstants.objectToProjection=mat4::identity();}
        rect(-.8f,.8f,.7f,vec4(0,1,0,1),false); // hidden by reflected red
        EndWaterReflection();
        SetSceneNode(&frame);pushconstants.objectToProjection=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
        rect(-.9f,.9f,.8f,vec4(0,0,1,1),false);
        vec3 points[]={{-.8f,-.8f,.2f},{.8f,-.8f,.2f},{.8f,.8f,.2f},{-.8f,.8f,.2f}};
        SurfaceInfo surface{};surface.Texture=&whiteInfo;surface.PolyFlags=split==2?PF_Occlude:PF_Translucent;
        SurfaceFacet facet{};facet.Water=true;facet.Vertices=points;facet.VertexCount=4;
        facet.MapCoords.Origin=vec3(0,0,.2f);facet.MapCoords.XAxis=vec3(1,0,0);facet.MapCoords.YAxis=vec3(0,1,0);
        DrawComplexSurface(&frame,surface,facet);
        Unlock(false);ReadPixels(pixels.data());
        auto left=pixels[(size_t)(h/2)*w+w/4],right=pixels[(size_t)(h/2)*w+w*3/4];
        LogMessage("RUNE_WATER_PIXELS split="+std::to_string(split)+" left="+std::to_string(left.R)+","+std::to_string(left.G)+","+std::to_string(left.B)+" right="+std::to_string(right.R)+","+std::to_string(right.G)+","+std::to_string(right.B));
        require(right.R>left.R+8 && std::abs(int(right.G)-int(left.G))<3,"reflection sampling and clip plane");
        if(split<2)require(right.B>right.R+50,"underwater background survives reflective water");
        else require(right.R>right.B+20,"opaque water retains authored opacity");
    }
    // Reuse the actual reflection in a fresh frame and verify projected pixels.
    WorldFeatures=10240;
    Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);SetLightingCamera(vec3(0,0,10));
    auto rendersBefore=WaterRenderCount;
    require(ReuseWaterReflection(.2f,1.26f,vec3(0,0,10)),"reflection reused inside time budget");
    require(Buffers->ShadowLighting->WaterParameters.x==1.26f,"waves advance with cached scene");
    SetSceneNode(&frame);pushconstants.objectToProjection=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
    vec3 cachedPoints[]={{-.8f,-.8f,.2f},{.8f,-.8f,.2f},{.8f,.8f,.2f},{-.8f,.8f,.2f}};
    SurfaceInfo cachedSurface{};cachedSurface.Texture=&whiteInfo;cachedSurface.PolyFlags=PF_Occlude;
    SurfaceFacet cachedFacet{};cachedFacet.Water=true;cachedFacet.Vertices=cachedPoints;cachedFacet.VertexCount=4;
    cachedFacet.MapCoords.Origin=vec3(0,0,.2f);cachedFacet.MapCoords.XAxis=vec3(1,0,0);cachedFacet.MapCoords.YAxis=vec3(0,1,0);
    DrawComplexSurface(&frame,cachedSurface,cachedFacet);Unlock(false);ReadPixels(pixels.data());
    require(pixels[(size_t)(h/2)*w+w*3/4].R>pixels[(size_t)(h/2)*w+w/4].R+8,"cached reflection survives buffer swap and lock reset");
    require(WaterRenderCount==rendersBefore,"reuse does not render another scene");
    require(!ReuseWaterReflection(.2f,1.30f,vec3(0,0,10)),"expired reflection refreshes");
    require(!ReuseWaterReflection(20,1.26f,vec3(0,0,10)),"new plane refreshes");
    require(!ReuseWaterReflection(.2f,1.26f,vec3(100,0,10)),"camera teleport refreshes");
    require(!ReuseWaterReflection(.2f,1.0f,vec3(0,0,10)),"level time reset refreshes");
    InvalidateWaterReflection();require(!ReuseWaterReflection(.2f,1.26f,vec3(0,0,10)),"map changes invalidate reflection");
    LogMessage("RUNE_WATER_CACHE_OK pixels=1 waves=1 no_scene_draw=1 time_budget=1 plane=1 teleport=1 map=1");
    int approximateFirst=-1;
    auto approximateRenders=WaterRenderCount;
    for(int mode=0;mode<3;++mode) {
        WorldFeatures=2048;
        Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
        SetLightingCamera(vec3(3,0,1));
        PrepareApproximateWater(.2f,mode==0?0.0f:2.0f);
        require(!BeginWaterReflection(&frame,.2f,2.0f),"approximate mode excludes scene reflection");
        SetSceneNode(&frame);pushconstants.objectToProjection=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
        rect(-.9f,.9f,.8f,vec4(0,0,1,1),false);
        cachedSurface.PolyFlags=mode==2?PF_Occlude:PF_Translucent;
        DrawComplexSurface(&frame,cachedSurface,cachedFacet);Unlock(false);ReadPixels(pixels.data());
        auto sample=pixels[(size_t)(h/2)*w+w/2];
        LogMessage("RUNE_WATER_APPROXIMATE_PIXEL mode="+std::to_string(mode)+" rgb="+std::to_string(sample.R)+","+std::to_string(sample.G)+","+std::to_string(sample.B));
        if(mode<2)require(sample.B>sample.R+25,"approximate water keeps submerged scenery visible");
        else require(std::abs(int(sample.R)-int(sample.B))<20,"approximate opaque water stays opaque");
        if(mode==0)approximateFirst=sample.R;
        if(mode==1)require(std::abs(int(sample.R)-approximateFirst)>1,"approximate ripples animate actual pixels");
        require(WaterRenderCount==approximateRenders,"approximate mode renders no reflection scene");
    }
    LogMessage("RUNE_WATER_APPROXIMATE_OK pixels=1 animated=1 transparency=1 opacity=1 scene_passes=0");
    if(commandline->HasArg("","--probe-modern-water")) RunModernWaterProbe();

    WorldFeatures=0;
    require(!BeginWaterReflection(&frame,0,0),"disabled water gate");
    LogMessage("RUNE_WATER_OK native=1 samples=4 reflection=1 clip=1 depth=1 transparency=1 continuation=1 toggle=1");
	WorldFeatures = savedWorld; LightMode = savedLightMode;
	LogMessage("RUNE_PIXEL_LIGHTING_OK native=1 samples=4 original=1 point_light=1 normals=1 two_sided=1 unlit=1 ambient=1 eight_lights=1 off_roundtrip=1 continuation=2");
	// Exercise the actual final shader, including screenshot readback. Flat
	// gray patches must survive sharpening; boundaries must change. Shadow
	// adjustment must lift dark/mid gray while preserving black and white.
	VisualEnhanced = false;
	GammaCorrectScreenshots = true;
	std::vector<TextureColor> clarityBaseline(baseline.size());
	size_t lowChanges = 0, mediumChanges = 0;
	int lowShadow = 0;
	for (int mode = 0; mode < 7; mode++)
	{
		AdaptiveSharpness = mode == 1 || mode == 5 ? 0.35f : mode == 2 ? 0.65f : 0.0f;
		ShadowLift = mode == 3 || mode == 5 ? 0.35f : mode == 4 ? 0.65f : 0.0f;
		TextureAnisotropy = mode % 2 ? 16.0f : 8.0f;
		Lock(vec4(0.5f), vec4(0.0f), vec4(0.0f), nullptr, nullptr);
		SetSceneNode(&frame);
		pushconstants.objectToProjection = mat4::identity();
		pushconstants.objectToView = mat4::identity();
		pushconstants.nearClip = vec4(0,0,0,1);
		const float grays[] = {0.0f, 0.05f, 0.1f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
		for (int band = 0; band < 8; band++)
			rect(-0.8f + band * 0.2f, -0.6f + band * 0.2f, 0.5f, vec4(grays[band],grays[band],grays[band],1), false);
		Unlock(false);
		ReadPixels(pixels.data());
		if (!mode) clarityBaseline = pixels;
		auto at = [&](const std::vector<TextureColor>& image, int band) {
			return image[(size_t)(h / 2) * w + w * (15 + band * 10) / 100].R;
		};
		require(std::abs(int(at(pixels, 0)) - at(clarityBaseline, 0)) <= 1, "clarity black preservation");
		require(std::abs(int(at(pixels, 7)) - at(clarityBaseline, 7)) <= 1, "clarity white preservation");
		if (mode == 1 || mode == 2)
		{
			for (int band = 1; band < 7; band++)
				require(std::abs(int(at(pixels, band)) - at(clarityBaseline, band)) <= 1, "sharpening flat gray preservation");
			size_t changes = 0;
			for (size_t i = 0; i < pixels.size(); i++)
				changes += std::abs(int(pixels[i].R) - clarityBaseline[i].R) > 1;
			if (mode == 1) lowChanges = changes; else mediumChanges = changes;
			require(changes > 0, "sharpening must affect gray boundaries");
		}
		if (mode == 3 || mode == 4 || mode == 5)
		{
			require(at(pixels, 2) > at(clarityBaseline, 2) && at(pixels, 4) > at(clarityBaseline, 4), "shadow and midtone lift");
			if (mode == 3) lowShadow = at(pixels, 2);
			if (mode == 4) require(at(pixels, 2) > lowShadow, "medium shadow strength");
		}
		if (mode == 6)
			for (size_t i = 0; i < pixels.size(); i++)
				require(std::abs(int(pixels[i].R) - clarityBaseline[i].R) <= 1 && std::abs(int(pixels[i].G) - clarityBaseline[i].G) <= 1 && std::abs(int(pixels[i].B) - clarityBaseline[i].B) <= 1, "clarity off round trip");
	}
	LogMessage("RUNE_CLARITY_PROBE_OK native=1 samples=4 black=1 white=1 flat_gray=1 shadows=1 midtones=1 off_roundtrip=1 sharp_low_changed=" + std::to_string(lowChanges) + " sharp_medium_changed=" + std::to_string(mediumChanges));
	AdaptiveSharpness = savedSharpness; ShadowLift = savedShadowLift; TextureAnisotropy = savedAnisotropy;
	InPassResolve = savedResolve;
	VisualEnhanced = savedEnhanced; Bloom = savedBloom; GammaCorrectScreenshots = savedGamma;
	HitX = savedHitX; HitY = savedHitY; HitWidth = savedHitWidth; HitHeight = savedHitHeight;
	CurrentFrame = nullptr;
	LogMessage("RUNE_VULKAN_PROBE_OK native=1 samples=4 resolve_paths=" + std::to_string(savedResolve ? 2 : 1) + " frames=" + std::to_string(savedResolve ? 48 : 24) + " continuation=2 depth=1 alpha=1 hit_before=1 hit_after=1 hit_miss=1 original=1 enhanced=1 capture_pixels=1");
}


void VulkanRenderDevice::RunMaterialLightingProbe()
{
    if(IsLocked || GetSettingsMultisample()!=4 || ResolutionScale!=1.0f)
        throw std::runtime_error("Material GPU probe requires unlocked native 4x renderer");
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Material probe: ")+message);};
    int savedWorld=WorldFeatures;bool savedGamma=GammaCorrectScreenshots,savedBloom=Bloom;
    float savedSharp=AdaptiveSharpness,savedLift=ShadowLift;
    Bloom=false;AdaptiveSharpness=ShadowLift=0;GammaCorrectScreenshots=false;
    int w=GetRenderWidth(),h=GetRenderHeight();
    SceneNode frame{};frame.X=w;frame.Y=h;frame.FX=float(w);frame.FY=float(h);frame.FX2=w*.5f;frame.FY2=h*.5f;frame.FovAngle=90;
    frame.ObjectToWorld=frame.WorldToView=mat4::identity();
    UnrealMipmap whiteMip;whiteMip.Width=whiteMip.Height=1;whiteMip.Data.resize(4);std::fill(whiteMip.Data.begin(),whiteMip.Data.end(),255);
    TextureInfo whiteInfo;whiteInfo.CacheID=0xfedcba9876543299ull;whiteInfo.Format=TextureFormat::BGRA8;whiteInfo.NumMips=1;whiteInfo.Mips=&whiteMip;
    std::vector<TextureColor> pixels(size_t(w)*h);
    // Exercise the real native/4x draw path, including roughness, independent
    // environment toggle, no direct lights, and an explicitly unlit receiver.
    for(int worldReceiver=0;worldReceiver<2;++worldReceiver) {
    int physicalPixels[6]{};
    for(int mode=0;mode<6;++mode) {
        WorldFeatures=768|(mode?65536:0)|(mode>=3?131072:0);
        Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
        SetLightingCamera(vec3(0,0,-100));SetSceneNode(&frame);
        pushconstants.objectToProjection=mat4::scale(.01f);pushconstants.nearClip=vec4(0,0,0,1);
        VertexLight lighting{};lighting.NumLights=mode==4?0:1;lighting.ScaleGlow=1;lighting.AmbientColor=vec3(.01f);
        lighting.Lights[0]={vec3(0,0,-20),1.0f/(250*250),vec3(.12f)};
        GouraudVertex verts[4];vec3 points[]={{-80,-80,60},{80,-80,60},{80,80,60},{-80,80,60}};
        for(int i=0;i<4;++i){verts[i].Point=points[i];verts[i].Normal=vec3(0,0,-1);verts[i].Lighting=&lighting;verts[i].Light=vec3(.1f);verts[i].Fog=vec4(0);verts[i].UV=vec2(points[i].x/160+.5f,points[i].y/160+.5f);}
        if(!worldReceiver || mode==5)DrawGouraudPolygon(&frame,whiteInfo,verts,4,PF_Occlude|(mode==5?PF_Unlit:0));
        else {
            SurfaceLight light{};light.Count=lighting.NumLights;
            light.Origin=vec3(-100,-100,60);light.U=vec3(200,0,0);light.V=vec3(0,200,0);light.Normal=vec3(0,0,-1);
            light.Lights[0]={vec3(0,0,-20),vec3(.12f),1.0f/(250*250),0};
            for(auto& mask:light.Shadows){mask.Width=mask.Height=1;mask.Data.resize(sizeof(vec4));vec4 visible(1);std::memcpy(mask.Data.data(),&visible,sizeof(visible));}
            UnrealMipmap mip;mip.Width=mip.Height=1;mip.Data.resize(sizeof(vec4));vec4 ambient(.01f);std::memcpy(mip.Data.data(),&ambient,sizeof(ambient));
            TextureInfo map;map.CacheID=0xfedcba9876544800ull;map.Format=TextureFormat::RGBA32_F;map.Mips=&mip;map.NumMips=1;map.UScale=map.VScale=200;map.bRealtimeChanged=true;map.PixelLighting=&light;
            SurfaceInfo surface;surface.Texture=&whiteInfo;surface.LightMap=&map;surface.PolyFlags=PF_Occlude;
            SurfaceFacet facet{};facet.Vertices=points;facet.VertexCount=4;facet.MapCoords.Origin=vec3(0,0,60);facet.MapCoords.XAxis=vec3(1,0,0);facet.MapCoords.YAxis=vec3(0,1,0);
            DrawComplexSurface(&frame,surface,facet);
        }
        if(mode!=5) {
            auto& packet=Buffers->MeshLights[Buffers->MeshLightCount-1];
            packet.Material=vec4(.7f,mode==0?48.0f:mode==2?.9f:.32f,.45f,mode==0?0.0f:1.0f);
            for(auto& lobe:packet.Environment)lobe=vec4(0);
            if(mode>=3) {packet.Environment[0].w=1;packet.Environment[5]=vec4(.8f,.12f,.04f,1);}
        }
        Unlock(false);ReadPixels(pixels.data());
        auto color=pixels[(size_t)(h/2)*w+w/2];physicalPixels[mode]=color.R;
        LogMessage("RUNE_PHYSICAL_MATERIAL_PIXEL world="+std::to_string(worldReceiver)+" mode="+std::to_string(mode)+" r="+std::to_string(color.R)+" b="+std::to_string(color.B));
        if(mode==4)require(color.R>color.B+10,"environment reflection supplies directional room color without a direct light");
        if(mode==5)require(std::abs(int(color.R)-int(color.B))<=1,"unlit effects bypass material/probe lighting");
    }
    require(std::abs(physicalPixels[0]-physicalPixels[1])>3,"physical material toggle changes real GPU shading");
    require(std::abs(physicalPixels[1]-physicalPixels[2])>3,"roughness changes physical highlights");
    require(physicalPixels[3]>physicalPixels[1]+3,"independent environment toggle adds room reflection");
    }
    LogMessage("RUNE_PHYSICAL_MATERIAL_GPU_OK native=1 samples=4 ggx=1 roughness=1 environment=1 no_direct_lights=1 unlit_bypass=1 mesh=1 bsp=1");

    WorldFeatures=savedWorld;Bloom=savedBloom;GammaCorrectScreenshots=savedGamma;AdaptiveSharpness=savedSharp;ShadowLift=savedLift;
}

void VulkanRenderDevice::RunModernWaterProbe()
{
    if(IsLocked || GetSettingsMultisample()!=4 || ResolutionScale!=1.0f || !UsesModernWater())
        throw std::runtime_error("Modern water probe requires enhanced water and an unlocked native 4x renderer");
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Modern water probe: ")+message);};
    const int savedWorld=WorldFeatures,savedLight=LightMode;
    const bool savedEnhanced=VisualEnhanced,savedBloom=Bloom,savedGamma=GammaCorrectScreenshots;
    const float savedSharp=AdaptiveSharpness,savedLift=ShadowLift;
    const vec3 savedCamera=CurrentLightingCamera;
    const int w=GetRenderWidth(),h=GetRenderHeight();
    SceneNode frame{};frame.X=w;frame.Y=h;frame.FX=float(w);frame.FY=float(h);frame.FX2=w*.5f;frame.FY2=h*.5f;frame.FovAngle=90;
    frame.ObjectToWorld=frame.WorldToView=mat4::identity();
    LightMode=0;VisualEnhanced=false;Bloom=false;GammaCorrectScreenshots=false;AdaptiveSharpness=ShadowLift=0;
    std::vector<TextureColor> pixels(size_t(w)*h);
    auto rect=[&](float x0,float x1,float z,vec4 color,bool alpha){
        SetPipeline(RenderPasses->GetPipeline(alpha?PF_Highlighted:PF_Occlude,false,false));
        auto binds=GetTextureIndexes(0,nullptr);auto alloc=ReserveVertices(4,6);require(alloc.vptr!=nullptr,"vertex allocation");
        vec3 points[]={{x0,-.8f,z},{x1,-.8f,z},{x1,.8f,z},{x0,.8f,z}};
        for(int i=0;i<4;++i)alloc.vptr[i] = {0u, points[i], vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color, binds};
        uint32_t indexes[]={0,1,2,0,2,3};for(int i=0;i<6;++i)alloc.iptr[i]=alloc.vpos+indexes[i];UseVertices(4,6);
    };
    UnrealMipmap whiteMip;whiteMip.Width=whiteMip.Height=1;whiteMip.Data.resize(4);for(auto& x:whiteMip.Data)x=255;
    TextureInfo whiteInfo;whiteInfo.CacheID=0xfedcba9876549910ull;whiteInfo.Format=TextureFormat::BGRA8;whiteInfo.NumMips=1;whiteInfo.Mips=&whiteMip;
    vec3 points[]={{-.8f,-.8f,.2f},{.8f,-.8f,.2f},{.8f,.8f,.2f},{-.8f,.8f,.2f}};
    SurfaceInfo cachedSurface{};cachedSurface.Texture=&whiteInfo;cachedSurface.PolyFlags=PF_Occlude;
    SurfaceFacet cachedFacet{};cachedFacet.Water=true;cachedFacet.Vertices=points;cachedFacet.VertexCount=4;
    cachedFacet.MapCoords.Origin=vec3(0,0,.2f);cachedFacet.MapCoords.XAxis=vec3(1,0,0);cachedFacet.MapCoords.YAxis=vec3(0,1,0);
        const auto oldRenders=WaterRenderCount;
        std::array<TextureColor,4> samples{};
        float depths[4]={16,16,16,16};cachedFacet.WaterDepth=depths;
        for(int mode=0;mode<4;++mode) {
            WorldFeatures=2048;
            Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
            require(Textures->Scene->SceneSamples==VK_SAMPLE_COUNT_4_BIT,"actual four-sample target");
            SetLightingCamera(vec3(3,0,1));
            PrepareModernWater(.2f,mode==1?2.f:0.f,vec4(.15f,.18f,.2f,1),vec4(.3f,-.4f,.86f,.4f),vec4(1));
            SetSceneNode(&frame);pushconstants.objectToProjection=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
            rect(-.9f,.9f,.8f,vec4(0,0,1,1),false);
            for(float& d:depths)d=mode>=2?512.f:16.f;
            cachedSurface.PolyFlags=mode==3?PF_Occlude:PF_Translucent;
            DrawComplexSurface(&frame,cachedSurface,cachedFacet);Unlock(false);ReadPixels(pixels.data());
            samples[mode]=pixels[(size_t)(h/2)*w+w/2];
            const auto& sample=samples[mode];
            LogMessage("RUNE_WATER_MODERN_PIXEL mode="+std::to_string(mode)+" rgb="+std::to_string(sample.R)+","+std::to_string(sample.G)+","+std::to_string(sample.B));
            require(WaterRenderCount==oldRenders,"modern water must not render a reflection scene");
        }
        require(samples[0].B>samples[0].R+40,"modern shallow water reveals blue background");
        require(samples[0].R!=samples[1].R || samples[0].G!=samples[1].G || samples[0].B!=samples[1].B,"modern water animates actual pixels");
        require(samples[0].B>samples[2].B+10,"depth changes modern water absorption/opacity");
        require(samples[3].B<220,"opaque authored water remains opaque");
        cachedFacet.WaterDepth=nullptr;
        LogMessage("RUNE_WATER_MODERN_OK native=1 samples=4 animation=1 depth=1 transparent=1 opaque=1 scene_passes=0");
        bool savedScreenshot=GammaCorrectScreenshots;
        bool savedSubmerged=WaterSubmerged;float savedWaterTime=WaterViewTime;
        std::vector<TextureColor> underwaterReference,underwaterFirst;
        for(int mode=0;mode<3;++mode) {
            WorldFeatures=2048;SetWaterView(mode!=0,mode==2?2.2f:1.2f);
            Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
            require(Textures->Scene->SceneSamples==VK_SAMPLE_COUNT_4_BIT,"actual four-sample target");
            SetSceneNode(&frame);pushconstants.objectToProjection=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
            for(int strip=0;strip<8;++strip)rect(-.8f+strip*.2f,-.6f+strip*.2f,.3f,strip%2?vec4(.1f,.8f,.2f,1):vec4(.8f,.1f,.6f,1),false);
            Unlock(false);GammaCorrectScreenshots=true;ReadPixels(pixels.data());
            if(mode==0)underwaterReference=pixels;
            if(mode==1)underwaterFirst=pixels;
        }
        size_t changed=0,edgeChanged=0,animated=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto at=size_t(y)*w+x;const auto& a=underwaterReference[at];const auto& b=pixels[at];
            const auto& first=underwaterFirst[at];if(first.R!=b.R || first.G!=b.G || first.B!=b.B)++animated;
            bool different=a.R!=b.R || a.G!=b.G || a.B!=b.B;
            if(different){++changed;if(x<w*.075f || x>w*.925f || y<h*.075f || y>h*.925f)++edgeChanged;}
        }
        require(changed>100,"underwater shimmer must change actual presentation pixels");
        require(edgeChanged==0,"underwater shimmer must preserve HUD-edge pixels");
        require(animated>100,"underwater refraction must animate actual pixels");
        GammaCorrectScreenshots=savedScreenshot;SetWaterView(savedSubmerged,savedWaterTime);
        LogMessage("RUNE_UNDERWATER_OK pixels=1 centre_changed="+std::to_string(changed)+" hud_edge_changes="+std::to_string(edgeChanged)+" extra_passes=0 animated_pixels="+std::to_string(animated));

    WorldFeatures=savedWorld;LightMode=savedLight;VisualEnhanced=savedEnhanced;Bloom=savedBloom;GammaCorrectScreenshots=savedGamma;
    AdaptiveSharpness=savedSharp;ShadowLift=savedLift;SetLightingCamera(savedCamera);
    LogMessage("RUNE_MODERN_WATER_PROBE_OK width="+std::to_string(w)+" height="+std::to_string(h)+" samples=4");
}
