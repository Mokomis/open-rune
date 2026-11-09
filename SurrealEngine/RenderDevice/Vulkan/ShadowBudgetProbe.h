#pragma once

// Actual GPU readback: distinct atlas tiles, last per-light bit lane, mesh/BSP
// receivers, moving/masked casters, grouped passes, and frame reset.
void VulkanRenderDevice::RunShadowBudgetProbe()
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Shadow budget probe: ")+message);};
    require(!IsLocked&&GetSettingsMultisample()==4&&ResolutionScale==1.0f,"requires unlocked native 4x renderer");
    const int savedWorld=WorldFeatures;const bool savedEnhanced=VisualEnhanced,savedBloom=Bloom,savedGamma=GammaCorrectScreenshots,savedGrouped=GroupShadowPasses;
    const float savedSharp=AdaptiveSharpness,savedLift=ShadowLift;
    VisualEnhanced=Bloom=GammaCorrectScreenshots=false;AdaptiveSharpness=ShadowLift=0;
    const int w=GetRenderWidth(),h=GetRenderHeight();std::vector<TextureColor> pixels(size_t(w)*h);
    SceneNode frame{};frame.X=w;frame.Y=h;frame.FX=float(w);frame.FY=float(h);frame.FX2=w*.5f;frame.FY2=h*.5f;frame.FovAngle=90;frame.ObjectToWorld=frame.WorldToView=mat4::identity();
    SceneNode lightFrame=frame;lightFrame.X=lightFrame.Y=ShadowSize;lightFrame.FX=lightFrame.FY=float(ShadowSize);lightFrame.Projection=mat4::scale(.01f);
    UnrealMipmap white;white.Width=white.Height=1;white.Data.resize(4);for(auto& value:white.Data)value=255;
    UnrealMipmap transparent=white;transparent.Data[3]=0;
    TextureInfo whiteInfo;whiteInfo.CacheID=0xfedcba9876547700ull;whiteInfo.Format=TextureFormat::BGRA8;whiteInfo.NumMips=1;whiteInfo.Mips=&white;whiteInfo.UScale=whiteInfo.VScale=1;
    TextureInfo maskInfo=whiteInfo;maskInfo.CacheID++;maskInfo.Mips=&transparent;
    int checks=0;int fadePixels[3]={};
    for(int world=0;world<2;++world)for(int target=0;target<RuneShadows::Capacity;++target)for(int side=0;side<(target==RuneShadows::Capacity-1?3:2);++side)for(int grouped=0;grouped<2;++grouped)
    for(int fadeCase=0;fadeCase<(!world && !target && !side && !grouped?3:1);++fadeCase){
        WorldFeatures=768;GroupShadowPasses=grouped!=0;
        VertexLight lighting{};lighting.AmbientColor=vec3(.02f);lighting.ScaleGlow=1;lighting.NumLights=8;
        for(int i=0;i<8;++i){lighting.Lights[i].Location=vec3(float(1000+i*100),1000,-20);lighting.Lights[i].InvRadiusSquared=1.0f/(250*250);}
        const int lane=target==RuneShadows::Capacity-1?7:target;
        lighting.Lights[lane].Location=vec3(0,0,-20);lighting.Lights[lane].Color=vec3(.3f);
        GouraudVertex receiver[4]{},caster[4]{};vec3 points[4]={{-80,-80,60},{80,-80,60},{80,80,60},{-80,80,60}};
        for(int i=0;i<4;++i){receiver[i].Point=points[i];receiver[i].Normal=vec3(0,0,-1);receiver[i].Lighting=&lighting;receiver[i].Light=vec3(.1f);receiver[i].UV=vec2(.5f);}
        Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);SetLightingCamera(vec3(0,0,-100));
        require(!BeginLightShadow(&lightFrame,vec3(0,0,-20),vec3(0),250,-1),"negative atlas slot");
        require(!BeginLightShadow(&lightFrame,vec3(0,0,-20),vec3(0),250,RuneShadows::Capacity),"out of range atlas slot");
        for(int slot=0;slot<RuneShadows::Capacity;++slot){
            // Non-target tiles deliberately contain the opposite silhouette.
            bool right=slot==target?side!=0:side==0;float x0=right?10.f:-75.f,x1=right?75.f:-10.f;
            vec3 c[4]={{x0,-80,20},{x1,-80,20},{x1,80,20},{x0,80,20}};
            for(int i=0;i<4;++i){caster[i]=receiver[i];caster[i].Point=c[i];}
            vec3 light=slot==target?vec3(0,0,-20):vec3(float(100+slot*20),0,-20);
            require(BeginLightShadow(&lightFrame,light,vec3(0,0,-100),250,slot),"valid atlas slot");
            if(slot==target && fadeCase)SetLightShadowStrength(slot,fadeCase==1?.5f:0.f);
            const bool masked=side==2&&slot==target;DrawGouraudPolygon(&lightFrame,masked?maskInfo:whiteInfo,caster,4,PF_Occlude|(masked?PF_Masked:0));EndLightShadow(slot+1<RuneShadows::Capacity);
        }
        SetSceneNode(&frame);pushconstants.objectToProjection=mat4::scale(.01f);pushconstants.objectToView=mat4::identity();pushconstants.nearClip=vec4(0,0,0,1);
        if(!world)DrawGouraudPolygon(&frame,whiteInfo,receiver,4,PF_Occlude);
        else{
            SurfaceLight light{};light.Count=8;light.Origin=vec3(-100,-100,60);light.U=vec3(200,0,0);light.V=vec3(0,200,0);light.Normal=vec3(0,0,-1);
            for(int i=0;i<8;++i)light.Lights[i]={lighting.Lights[i].Location,lighting.Lights[i].Color,lighting.Lights[i].InvRadiusSquared,0};
            for(auto& mask:light.Shadows){mask.Width=mask.Height=1;mask.Data.resize(sizeof(vec4));vec4 visible(1);std::memcpy(mask.Data.data(),&visible,sizeof(visible));}
            UnrealMipmap mip;mip.Width=mip.Height=1;mip.Data.resize(sizeof(vec4));vec4 base(.04f);std::memcpy(mip.Data.data(),&base,sizeof(base));
            TextureInfo map;map.CacheID=0xfedcba9876547710ull;map.Format=TextureFormat::RGBA32_F;map.Mips=&mip;map.NumMips=1;map.UScale=map.VScale=200;map.bRealtimeChanged=true;map.PixelLighting=&light;
            SurfaceInfo surface;surface.Texture=&whiteInfo;surface.LightMap=&map;surface.PolyFlags=PF_Occlude;
            SurfaceFacet facet{};facet.Vertices=points;facet.VertexCount=4;facet.MapCoords.Origin=vec3(0,0,60);facet.MapCoords.XAxis=vec3(1,0,0);facet.MapCoords.YAxis=vec3(0,1,0);
            DrawComplexSurface(&frame,surface,facet);
        }
        require(RuneShadows::Slots::Decode(std::bit_cast<uint32_t>(Buffers->MeshLights[Buffers->MeshLightCount-1].Meta.w),lane)==(fadeCase==2?-1:target),"receiver slot encoding");
        Unlock(false);ReadPixels(pixels.data());int left=pixels[size_t(h/2)*w+w/4].R,right=pixels[size_t(h/2)*w+3*w/4].R;
        LogMessage("RUNE_SHADOW_BUDGET_PIXEL world="+std::to_string(world)+" slot="+std::to_string(target)+" lane="+std::to_string(lane)+" side="+std::to_string(side)+" grouped="+std::to_string(grouped)+" left="+std::to_string(left)+" right="+std::to_string(right));
        if(!world && !target && !side && !grouped)fadePixels[fadeCase]=left;
        if(fadeCase==2)require(std::abs(left-right)<=2,"zero-strength shadow is invisible");
        else if(fadeCase==1)require(left>fadePixels[0]+20 && left<right-20,"half-strength shadow interpolates");
        else {if(side==2)require(std::abs(left-right)<=2,"masked last atlas tile");else require(side?right+40<left:left+40<right,"tile isolation and moving shadow");++checks;}
    }
    // A new frame with no shadow maps must never sample stale tiles.
    LogMessage("RUNE_SHADOW_FADE_GPU_OK full="+std::to_string(fadePixels[0])+" half="+std::to_string(fadePixels[1])+" zero="+std::to_string(fadePixels[2]));
    WorldFeatures=768;Lock(vec4(.5f),vec4(0),vec4(0),nullptr,nullptr);
    for(const auto& shadow:Buffers->ShadowLighting->Shadows)require(shadow.LightPosition.w==0,"inactive frame slot reset");
    Unlock(false);
    WorldFeatures=savedWorld;VisualEnhanced=savedEnhanced;Bloom=savedBloom;GammaCorrectScreenshots=savedGamma;GroupShadowPasses=savedGrouped;AdaptiveSharpness=savedSharp;ShadowLift=savedLift;
    LogMessage("RUNE_SHADOW_BUDGET_GPU_OK capacity="+std::to_string(RuneShadows::Capacity)+" cases="+std::to_string(checks)+" columns="+std::to_string(RuneShadows::Columns)+" rows="+std::to_string(RuneShadows::Rows)+" native=1 samples=4");
}
