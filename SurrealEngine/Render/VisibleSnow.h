#pragma once
#include "RuneSnow.h"
#include "VisibleFrame.h"
#include "VisibleRuneEffects.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Utils/CommandLine.h"
#include <chrono>
#include <array>
#include <limits>

namespace RuneSnow {
struct Column {int x=0,y=0,band=0;bool valid=false,open=false;float bottom=0,top=0,light=.65f;double checked=-100;};
struct Weather {
    static constexpr int Width=21,Radius=10;
    std::array<Column,Width*Width> cells{};
    bool sky=false;unsigned frames=0;double cpu=0;unsigned quads=0,traces=0;
};
inline Weather weather;
inline void Reset() {
    weather={};
    if(!engine->LaunchInfo.IsRune() || !engine->Level || !engine->Level->Model)return;
    for(auto& s:engine->Level->Model->Surfaces)if(s.PolyFlags&PF_FakeBackdrop){weather.sky=true;break;}
}
inline CollisionHit Cast(vec3 from,vec3 to) {
    TraceFlags flags;flags.world=true;
    return engine->Level->Collision.TraceFirstHit(from,to,nullptr,vec3(0),flags);
}
inline bool Sky(const CollisionHit& hit) {
    return hit.Actor==engine->LevelInfo && hit.Node && hit.Node->Surf>=0 &&
        size_t(hit.Node->Surf)<engine->Level->Model->Surfaces.size() &&
        (engine->Level->Model->Surfaces[hit.Node->Surf].PolyFlags&PF_FakeBackdrop);
}
// Five footprint samples conservatively reject mixed sky/roof cells. Static BSP results survive until
// the cell/height band changes; moving roofs are culled separately each frame.
inline Column Sample(int x,int y,int band,double time) {
    Column c;c.x=x;c.y=y;c.band=band;c.valid=true;c.checked=time;c.open=true;
    c.bottom=-std::numeric_limits<float>::infinity();c.top=std::numeric_limits<float>::infinity();
    const float z=band*64.f+32;
    for(auto offset:std::array<vec2,5>{vec2(32,32),vec2(1,1),vec2(63,1),vec2(1,63),vec2(63,63)}) {
        // Start above local roofs/terrain so flakes remain visible above them.
        // A low sky ceiling may sit below that point: retry at eye height.
        vec3 p(x*64.f+offset.x,y*64.f+offset.y,z+384);
        auto up=Cast(p,p+vec3(0,0,32768));++weather.traces;
        if(!Sky(up)){p.z=z;up=Cast(p,p+vec3(0,0,32768));++weather.traces;}
        if(!Sky(up)){c.open=false;break;}
        c.top=std::min(c.top,p.z+32768*up.Fraction-8);
        auto down=Cast(p,p-vec3(0,0,32768));++weather.traces;
        if(!down.Actor || down.Fraction>=1){c.open=false;break;}
        c.bottom=std::max(c.bottom,p.z-32768*down.Fraction+5);
    }
    auto region=engine->Level->Model->FindRegion(vec3(x*64.f+32,y*64.f+32,z),engine->LevelInfo);
    if(region.Zone){c.light=.48f+.32f*float(region.Zone->AmbientBrightness())/255.f;if(region.Zone->bWaterZone())c.open=false;}
    if(c.bottom>=c.top)c.open=false;
    return c;
}
inline size_t Index(int x,int y) {auto mod=[](int n){return (n%Weather::Width+Weather::Width)%Weather::Width;};return mod(y)*Weather::Width+mod(x);}
inline const std::vector<ivec2>& Offsets() {
    static auto offsets=[] {std::vector<ivec2> r;for(int y=-Weather::Radius;y<=Weather::Radius;y++)for(int x=-Weather::Radius;x<=Weather::Radius;x++)if(x*x+y*y<=Weather::Radius*Weather::Radius)r.emplace_back(x,y);
        std::stable_sort(r.begin(),r.end(),[](auto a,auto b){return a.x*a.x+a.y*a.y<b.x*b.x+b.y*b.y;});return r;}();return offsets;
}
struct Flake {vec3 p;float size,opacity,light,depth;};
inline void Draw(VisibleFrame* frame) {
    int mode=std::clamp(Mode.load(),0,2);
    if(!mode || !weather.sky || frame->ShadowView || frame->PortalDepth || frame->MirrorFlag || !engine->LaunchInfo.IsRune())return;
    // Opt-in stress preset; shipped Off/Light/Heavy settings are unchanged.
    const bool blizzard=commandline->HasArg("","--snow-blizzard");
    const int density=blizzard?120:FlakesPerCell(mode), limit=blizzard?18000:MaxFlakes(mode);
    const float coverageLimit=blizzard?.55f:CoverageLimit(mode);
    auto started=std::chrono::steady_clock::now();
    const auto eye=frame->ViewLocation.xyz();int cx=int(std::floor(eye.x/64)),cy=int(std::floor(eye.y/64)),band=int(std::floor(eye.z/64));
    const double time=engine->TotalTime;int refresh=8;unsigned drawCount=0;
    const float tanX=std::tan(frame->Frame.FovAngle*3.14159265f/360.f),tanY=tanX*frame->Frame.FY/frame->Frame.FX;
    std::vector<Flake> flakes;flakes.reserve(blizzard?12000:MaxFlakes(mode));
    const float gust=std::sin(float(time)*.31f);
    // Immediate conservative shelter for moving trapdoors/lifts. No ray per
    // flake, no delayed roof-cache invalidation, and no collision callbacks.
    std::vector<BBox> roofs;
    for(auto* actor:engine->Level->Actors)if(actor && !actor->bDeleteMe())if(auto* b=GetMovingBrush(actor);b && b->Brush() && (b->bBlockActors() || b->bBlockPlayers())) {
        auto box=b->Brush()->BoundingBox.transform(GetMovingBrushTransform(b));
        if(box.max.x>=eye.x-700 && box.min.x<=eye.x+700 && box.max.y>=eye.y-700 && box.min.y<=eye.y+700 && box.max.z>eye.z-384)roofs.push_back(box);
    }
    for(auto o:Offsets()) {
        int x=cx+o.x,y=cy+o.y;auto& c=weather.cells[Index(x,y)];
        bool match=c.valid && c.x==x && c.y==y && c.band==band;
        if(refresh>0 && !match) {c=Sample(x,y,band,time);--refresh;match=true;}
        if(!match || !c.open)continue;
        uint32_t seed=Hash(uint32_t(x)*0x9e3779b9u^uint32_t(y)*0x85ebca6bu);
        for(int i=0;i<density;i++) {
            uint32_t h=seed+uint32_t(i)*7919u;
            // Keep lateral drift inside the tested footprint; the lattice and
            // falling phases are world anchored and don't follow camera turns.
            float phase=Unit(h+3)*6.2831853f;
            vec3 p;
            if(blizzard) {
                // Strong gusts remain inside the sampled footprint, preserving
                // roof rejection even during the extreme density test.
                p.x=x*64.f+29+Unit(h+4)*6+20*std::sin(float(time)*1.8f+phase)+8*gust;
                p.y=y*64.f+24+Unit(h+5)*16+18*std::cos(float(time)*1.3f+phase);
                p.z=Height(float(time)*2.3f,h,eye.z);
            } else {
                p=vec3(x*64.f+8+Unit(h+4)*48+5*std::sin(float(time)*.7f+phase)+2*gust,
                    y*64.f+8+Unit(h+5)*48+5*std::cos(float(time)*.53f+phase),Height(float(time),h,eye.z));
            }
            if(p.z<c.bottom || p.z>c.top)continue;
            bool sheltered=false;for(const auto& roof:roofs)if(p.x>=roof.min.x-2 && p.x<=roof.max.x+2 && p.y>=roof.min.y-2 && p.y<=roof.max.y+2 && p.z<roof.max.z+2){sheltered=true;break;}
            if(sheltered)continue;
            vec3 delta=p-eye;float depth=dot(delta,frame->ViewRotation.XAxis);
            if(depth<35 || depth>650)continue;
            float size=(.65f+Unit(h+6)*1.35f)*(blizzard?1.45f:1.f);
            float extent=size*(blizzard?2.5f:1.f);
            if(std::abs(dot(delta,frame->ViewRotation.YAxis))>depth*tanX+extent || std::abs(dot(delta,frame->ViewRotation.ZAxis))>depth*tanY+extent)continue;
            float fade=std::min({std::clamp((depth-35)/45.f,0.f,1.f),std::clamp((650-depth)/160.f,0.f,1.f),std::clamp((384-std::abs(delta.z))/80.f,0.f,1.f),std::clamp((p.z-c.bottom)/15.f,0.f,1.f),std::clamp((c.top-p.z)/20.f,0.f,1.f)});
            if(fade<=.01f)continue;
            flakes.push_back({p,size,fade*(.5f+.35f*Unit(h+7)),blizzard?std::max(c.light,.78f):c.light,depth});
        }
    }
    std::sort(flakes.begin(),flakes.end(),[](auto a,auto b){return a.depth>b.depth;});
    auto texture=RuneEffects::Texture(0,false);float coverage=0;
    frame->Device->SetSceneNode(&frame->Frame);
    for(const auto& f:flakes) {
        float area=f.size*f.size*(blizzard?1.8f:1.f)/(f.depth*f.depth*tanX*tanY);
        if(drawCount>=unsigned(limit) || coverage+area>coverageLimit)continue;
        coverage+=area;
        auto side=frame->ViewRotation.YAxis*f.size,up=frame->ViewRotation.ZAxis*f.size;
        if(blizzard)up=up*1.8f+side*.65f;
        GouraudVertex v[4];v[0].Point=f.p-side-up;v[1].Point=f.p+side-up;v[2].Point=f.p+side+up;v[3].Point=f.p-side+up;
        v[0].UV={0,0};v[1].UV={128,0};v[2].UV={128,128};v[3].UV={0,128};
        for(auto& vertex:v){vertex.Light=vec3(.88f,.94f,1.f)*f.light;vertex.Fog=vec4(0);vertex.Opacity=f.opacity;}
        frame->Device->DrawGouraudPolygon(&frame->Frame,texture,v,4,PF_TwoSided|PF_Unlit|PF_Highlighted);
        ++drawCount;
    }
    weather.quads+=drawCount;weather.cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(++weather.frames%300==0 && commandline->HasArg("","--snow-profile")) {
        LogMessage("RUNE_SNOW mode="+std::to_string(mode)+" blizzard="+std::to_string(blizzard)+" cpu_ms="+std::to_string(weather.cpu/300)+" quads="+std::to_string(weather.quads/300)+" traces="+std::to_string(weather.traces)+" coverage="+std::to_string(coverage));
        weather.cpu=0;weather.quads=weather.traces=0;
    }
}
}
