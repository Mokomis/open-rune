#include "Precomp.h"
#include "RuneGore.h"
#include "GorePhysics.h"
#include "RuneBloodPresentation.h"
#include "Render/VisibleFrame.h"
#include "Render/VisibleRuneEffects.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Package/PackageManager.h"
#include "Utils/Logger.h"
#include <array>
#include <map>
#include <chrono>

namespace RuneGore {
namespace {
constexpr int MaxFragments=24,MaxStains=128,TraceBudget=96;
enum Kind {Other,Gib,Limb,Corpse,PawnCandidate};
struct Tracked {
    UActor* actor=nullptr;Kind kind=Other;double born=0,nextTrail=0,nextGround=0;
    bool red=false,pool=false;unsigned seen=0;
    vec3 visualAngles=vec3(0);float correction=0,target=0;
};
struct Fragment {GorePhysics::Body body;VertexLight lighting{};vec3 shape=vec3(1);bool pool=false;};
struct Stain {bool live=false;vec3 point=vec3(0),normal=vec3(0,0,1);float radius=1;double born=0;bool pool=false;};
std::map<int,Tracked> actors;
std::map<UClass*,Kind> kinds;
std::map<UTexture*,bool> capTextures;
std::array<Fragment,MaxFragments> fragments;
std::array<Stain,MaxStains> stains;
struct BloodEvidence {vec3 point=vec3(0);double until=0;};
std::array<BloodEvidence,32> evidence;size_t evidenceCursor=0;
std::map<int,UActor*> bloodActors;
size_t stainCursor=0,fragmentCursor=0;
Statistics stats;
unsigned serial=0;bool baseline=false,allowed=false;
float frameDt=0;double now=0;
std::map<UTexture*,std::unique_ptr<std::array<TextureColor,256>>> woundPalettes;

Kind Type(UActor* a) {
    auto it=kinds.find(a->Class);if(it!=kinds.end())return it->second;
    Kind k=a->IsA("LimbWeapon")?Limb:a->IsA("GibRedMedium")?Gib:a->IsA("Carcass")?Corpse:a->IsA("Pawn")?PawnCandidate:Other;
    kinds[a->Class]=k;return k;
}
bool Loose(UActor* a) {return !a->bCarriedItem() && (!a->HasProperty("AttachParent") || !a->GetUObject("AttachParent"));}
bool Finished(UActor* a) {
    return a->bAnimFinished() || std::abs(a->AnimRate())<.001f ||
        (!a->bAnimLoop() && a->AnimFrame()>=a->AnimLast()-.001f);
}
bool Resting(UActor* a) {
    if(a->CorpseBody.active || a->CorpseBody.presented || a->bCarriedItem() || (a->NetworkReplica && length(a->Velocity())>3.f))return false;
    return a->Physics()==PHYS_None ||
        (a->IsA("Pawn") && a->GetInt("Health")<=0 && a->Physics()==PHYS_Walking && length(a->Velocity())<3);
}
bool Red(UActor* a) {
    std::string name=a->Class->Name.ToString();
    if(a->HasProperty("Skeletal"))if(auto* model=a->GetUObject("Skeletal"))name+=model->Name.ToString();
    for(char& c:name)c=char(std::tolower((unsigned char)c));
    return name.find("sark")==std::string::npos && name.find("green")==std::string::npos;
}
CollisionHit Trace(vec3 from,vec3 to,float radius=0) {
    ++stats.traces;TraceFlags flags;flags.world=true;
    return engine->Level->Collision.TraceFirstHit(from,to,nullptr,vec3(radius),flags);
}
bool Mark(vec3 point,vec3 normal,float radius,bool pool=false) {
    if(stats.traces+5>TraceBudget || dot(normal,normal)<.5f)return false;
    normal=normalize(normal);vec3 u=GorePhysics::Tangent(normal),v=cross(normal,u);
    // Reject overhanging corners, steps and moving surfaces. Persisted stains
    // never detach from a lift or span empty space beside a stair tread.
    for(vec3 corner:{u+v,u-v,-u+v,-u-v}) {
        auto hit=Trace(point+corner*radius+normal*2.f,point+corner*radius-normal*2.f);
        if(hit.Actor!=engine->LevelInfo || hit.Fraction>=1 || std::abs(hit.Fraction-.5f)>.2f || dot(hit.Normal,normal)<.9f)return false;
    }
    stains[stainCursor++%stains.size()]={true,point+normal*.25f,normal,radius,now,pool};++stats.stains;
    return true;
}
void Burst(UActor* a,int count) {
    if(count<=0)return;
    VertexLight lighting{};engine->Level->Light.UpdateLightList(a);
    engine->Level->Light.InitVertexLight(lighting,a,engine->GetZoneActor(a->Region().ZoneNumber));
    lighting.NumLights=std::min(lighting.NumLights,2);
    // Independent integer hash: never consume or reseed gameplay randomness.
    uint32_t seed=uint32_t(a->Index)*747796405u+2891336453u;
    auto unit=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed&65535)/65535.f;};
    for(int n=0;n<count;++n) {
        auto slot=std::find_if(fragments.begin(),fragments.end(),[](const Fragment& f){return !f.body.live;});
        if(slot==fragments.end())break;
        auto& f=*slot;f={};auto& b=f.body;b.live=true;b.position=a->Location();
        float angle=unit()*6.2831853f;
        b.radius=1.5f+unit()*2.f;b.position.z+=b.radius*1.5f;
        b.velocity=a->Velocity()*.55f+vec3(std::cos(angle)*(40+unit()*70),std::sin(angle)*(40+unit()*70),65+unit()*95);
        float speed=length(b.velocity);if(speed>450)b.velocity*=450/speed;
        b.spin=vec3(unit()-.5f,unit()-.5f,unit()-.5f)*9.f;
        b.angles=vec3(unit(),unit(),unit())*6.2831853f;
        f.shape=vec3(.7f+unit()*.5f,.55f+unit()*.4f,1.1f+unit()*.5f);f.lighting=lighting;
        ++stats.fragments;
    }
    ++stats.bursts;
}
TextureInfo StainTexture() {
    struct Image {
        std::array<UnrealMipmap,8> mips;
        Image() {
            auto& m=mips[0];m.Width=m.Height=128;m.Data.resize(128*128*4);
            for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                float u=(x+.5f)/64-1,v=(y+.5f)/64-1,angle=std::atan2(v,u),r=std::sqrt(u*u+v*v);
                float edge=.70f+.09f*std::sin(angle*5)+.055f*std::sin(angle*9+1.3f);
                float alpha=std::clamp((edge-r)*16.f,0.f,1.f)*(.86f+.10f*std::sin(u*11+v*7));
                for(vec2 p:{vec2(.69f,.46f),vec2(-.66f,.51f),vec2(.35f,-.79f)}) {
                    float d=length(vec2(u,v)-p);alpha=std::max(alpha,std::clamp((.09f-d)*24.f,0.f,.8f));
                }
                auto c=uint8_t(std::clamp(alpha,0.f,1.f)*255);size_t i=(y*128+x)*4;
                m.Data[i]=m.Data[i+1]=m.Data[i+2]=m.Data[i+3]=c;
            }
            for(int level=1;level<8;++level) {
                auto& p=mips[level-1];auto& q=mips[level];q.Width=q.Height=p.Width/2;q.Data.resize(q.Width*q.Height*4);
                for(int y=0;y<q.Height;++y)for(int x=0;x<q.Width;++x)for(int c=0;c<4;++c) {
                    int sum=0;for(int j=0;j<2;++j)for(int i=0;i<2;++i)sum+=p.Data[((y*2+j)*p.Width+x*2+i)*4+c];
                    q.Data[(y*q.Width+x)*4+c]=uint8_t((sum+2)/4);
                }
            }
        }
    };
    static Image image;TextureInfo info;info.CacheID=uint64_t(uintptr_t(&image));info.Format=TextureFormat::RGBA8_;
    info.USize=info.VSize=128;info.Mips=image.mips.data();info.NumMips=8;return info;
}
float Angle(float current,float target,float fraction) {
    float delta=std::remainder(target-current,65536.f);return current+delta*fraction;
}
}

void Reset() {
    actors.clear();kinds.clear();capTextures.clear();fragments={};stains={};woundPalettes.clear();evidence={};evidenceCursor=0;bloodActors.clear();
    stainCursor=fragmentCursor=0;stats={};serial=0;baseline=allowed=false;frameDt=0;now=0;
}
Statistics Stats() {return stats;}

void Tick(float elapsed) {
    allowed=Enabled.load() && engine->LaunchInfo.IsRune();
    if(allowed) {
        auto* rules=engine->packages->FindClass("Engine.GameInfo")->GetDefaultObject<UObject>();
        allowed=!rules->GetBool("bVeryLowGore") && !rules->GetBool("bLowGore");
    }
    // A zero-time redraw is observational: preserve transient history for
    // exact same-pose comparisons and pause-menu previews.
    if(!std::isfinite(elapsed)||elapsed<=0)return;
    if(!allowed){actors.clear();fragments={};baseline=false;stats.active=stats.sleeping=stats.drawn=stats.traces=0;stats.updateUs=0;if(Enabled.load()){stains={};stainCursor=0;}return;}
    struct Timer {
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        ~Timer(){stats.updateUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();}
    } timer;
    frameDt=std::min(elapsed,.1f);now=engine->LevelInfo->TimeSeconds();++serial;
    stats.traces=stats.active=stats.sleeping=stats.drawn=0;
    std::vector<vec3> blood;
    std::map<int,UActor*> recentBlood;
    for(auto* a:engine->Level->Actors)if(a && !a->bDeleteMe() && RuneBloodPresentation::RedEmitter(a) &&
        a->HasProperty("CurrentTime") && a->GetFloat("CurrentTime")<.25f && blood.size()<32) {
        blood.push_back(a->Location());recentBlood[a->Index]=a;
        auto prior=bloodActors.find(a->Index);
        if(prior==bloodActors.end() || prior->second!=a)evidence[evidenceCursor++%evidence.size()]={a->Location(),now+1.5};
    }
    bloodActors=std::move(recentBlood);
    int emissionBudget=8;
    for(auto* a:engine->Level->Actors) {
        if(!a || a->bDeleteMe() || a->bHidden())continue;
        auto kind=Type(a);if(kind==Other)continue;
        if(kind==PawnCandidate){if(a->GetInt("Health")>0)continue;kind=Corpse;}
        if(!Loose(a))continue;
        auto found=actors.find(a->Index);
        bool fresh=found==actors.end() || found->second.actor!=a;
        if(fresh && actors.size()>=128)continue;
        auto& t=actors[a->Index];
        if(fresh) {
            t={};t.actor=a;t.kind=kind;t.born=now;t.visualAngles=vec3(float(a->Rotation().Pitch),float(a->Rotation().Yaw),float(a->Rotation().Roll));
            t.red=Red(a) && (kind!=Corpse || std::any_of(evidence.begin(),evidence.end(),[&](const BloodEvidence& e){return e.until>now && length(e.point-a->Location())<128;}));
            if(baseline && t.red && !(a->Region().Zone && a->Region().Zone->bWaterZone()) && emissionBudget>0 && length(a->Location()-engine->CameraLocation)<1400) {
                int count=std::min(emissionBudget,kind==Corpse?2:4);Burst(a,count);emissionBudget-=count;
            }
        }
        t.seen=serial;
        float blend=1-std::exp(-frameDt*14);
        // Preserve authoritative transforms; only ease the mesh after the death
        // animation, including the original scripted ground-alignment snap.
        if(kind==Corpse && now-t.born>1 && Finished(a) && Resting(a)) {
            vec3 target(float(a->Rotation().Pitch),float(a->Rotation().Yaw),float(a->Rotation().Roll));
            for(int i=0;i<3;++i)t.visualAngles[i]=Angle(t.visualAngles[i],target[i],blend);
            t.correction+=(t.target-t.correction)*blend;
        } else {t.visualAngles=vec3(float(a->Rotation().Pitch),float(a->Rotation().Yaw),float(a->Rotation().Roll));t.target=t.correction=0;}
        auto* zone=a->Region().Zone;
        if(t.red && !(zone && zone->bWaterZone()) && now<t.born+1.3 && now>=t.nextTrail && length(a->Velocity())>70 && stats.traces+6<=TraceBudget) {
            t.nextTrail=now+.16;
            auto hit=Trace(a->Location(),a->Location()-vec3(0,0,80));
            if(hit.Actor==engine->LevelInfo && hit.Fraction<1 && hit.Normal.z>.6f)
                Mark(a->Location()-vec3(0,0,80)*hit.Fraction,hit.Normal,1.4f+float(a->Index%3)*.4f);
        }
        if(t.red && !t.pool && now-t.born>1 && Resting(a) && !(zone&&zone->bWaterZone()) && stats.traces+6<=TraceBudget) {
            auto hit=Trace(a->Location()+vec3(0,0,8),a->Location()-vec3(0,0,96));
            t.pool=true;
            if(hit.Actor==engine->LevelInfo && hit.Fraction<1 && hit.Normal.z>.6f)
                Mark(a->Location()+vec3(0,0,8)-vec3(0,0,104)*hit.Fraction,hit.Normal,kind==Corpse?11.f:4.f,true);
        }
    }
    for(auto it=actors.begin();it!=actors.end();)if(it->second.seen!=serial)it=actors.erase(it);else ++it;
    baseline=true;
    for(size_t n=0,start=fragmentCursor++%fragments.size();n<fragments.size();++n) {
        auto& f=fragments[(start+n)%fragments.size()];if(!f.body.live)continue;
        auto& b=f.body;
        auto region=engine->Level->Model->FindRegion(b.position,engine->LevelInfo);
        if(region.Zone && region.Zone->bWaterZone()){b.live=false;continue;}
        if(stats.traces+24>TraceBudget && !b.sleeping){b.age+=frameDt;if(b.age>7)b.live=false;continue;}
        auto hit=GorePhysics::Step(b,frameDt,vec3(0,0,-950),[](vec3 a,vec3 z,float radius){
            auto h=Trace(a,z,radius);return GorePhysics::Hit{h.Actor && h.Fraction<1,h.Fraction,h.Normal,h.Actor==engine->LevelInfo};
        });
        if(hit.hit && hit.speed>70 && stats.traces+5<=TraceBudget){++stats.impacts;Mark(hit.position,hit.normal,2.5f);}
        if(b.sleeping && !f.pool && stats.traces+5<=TraceBudget) {
            f.pool=true;auto h=Trace(b.position,b.position-vec3(0,0,b.radius+2));
            if(h.Actor==engine->LevelInfo && h.Fraction<1)Mark(b.position-vec3(0,0,b.radius+2)*h.Fraction,h.Normal,b.radius*1.8f,true);
        }
        if(b.live){++stats.active;if(b.sleeping)++stats.sleeping;}
    }
}

mat4 MeshTransform(UActor* a,UMesh* mesh,const mat4& original) {
    if(!allowed || !Enabled.load())return original;
    auto found=actors.find(a->Index);if(found==actors.end() || found->second.actor!=a || !Loose(a))return original;
    auto& t=found->second;
    if(t.kind==Gib && a->Physics()==PHYS_Falling) {
        // Extra pitch/roll around the authoritative center adds tumbling to the
        // original yaw-only spin without moving the actor or its hit cylinder.
        float age=float(now-t.born),speed=std::min(length(a->Velocity()),300.f)/300.f;
        float pitch=std::sin(age*7+float(a->Index%7))*.65f*speed;
        float roll=std::sin(age*5+float(a->Index%5))*.8f*speed;
        auto center=a->Location();
        return mat4::translate(center)*mat4::rotate(pitch,0,1,0)*mat4::rotate(roll,1,0,0)*mat4::translate(-center)*original;
    }
    if(t.kind!=Corpse || now-t.born<=1 || !Resting(a) || !Finished(a))return original;
    if(now>=t.nextGround && stats.traces<TraceBudget && mesh && !mesh->Verts.empty()) {
        t.nextGround=now+.3;
        auto p=a->Location();auto h=Trace(p+vec3(0,0,8),p-vec3(0,0,96));
        if(h.Actor==engine->LevelInfo && h.Fraction<1 && h.Normal.z>.85f) {
            float floor=p.z+8-104*h.Fraction,lowest=1e30f;
            auto* groups=a->HasProperty("SkelGroupFlags")?static_cast<int*>(a->GetProperty("SkelGroupFlags")):nullptr;
            for(const auto& tri:mesh->Tris)if(!groups || !(groups[tri.TextureIndex]&PF_Invisible))
                for(auto index:tri.Indices)if(size_t(index)<mesh->Verts.size())lowest=std::min(lowest,(original*vec4(mesh->Verts[index],1)).z);
            float correction=floor+.3f-lowest;
            if(std::isfinite(correction) && std::abs(correction)<10) {t.target=std::clamp(correction,-6.f,3.f);++stats.settles;}
        }
    }
    Rotator diff(int(std::remainder(t.visualAngles.x-a->Rotation().Pitch,65536.f)),int(std::remainder(t.visualAngles.y-a->Rotation().Yaw,65536.f)),int(std::remainder(t.visualAngles.z-a->Rotation().Roll,65536.f)));
    if(diff==Rotator(0,0,0) && t.correction==0)return original;
    auto p=a->Location();
    return mat4::translate(p+vec3(0,0,t.correction))*Coords::Rotation(diff).ToMatrix()*mat4::translate(-p)*original;
}

void Material(TextureInfo& info,UActor* a) {
    if(!allowed || !Enabled.load() || !info.Texture)return;
    auto tag=capTextures.find(info.Texture);
    if(tag==capTextures.end()) {
        std::string name=info.Texture->Name.ToString();for(char& c:name)c=char(std::tolower((unsigned char)c));
        tag=capTextures.emplace(info.Texture,name.find("gore")!=std::string::npos).first;
    }
    bool cap=tag->second;
    if((!cap && Type(a)!=Gib) || !Red(a))return;
    info.WetGore=true;
    if(!info.Palette || info.Format!=TextureFormat::P8)return;
    auto found=woundPalettes.find(info.Texture);
    if(found==woundPalettes.end()) {
        if(woundPalettes.size()>=64)return;
        auto palette=std::make_unique<std::array<TextureColor,256>>();
        for(int i=0;i<256;++i) {
            auto c=info.Palette[i];float r=c.R/255.f,g=c.G/255.f,b=c.B/255.f;
            // Retain pale bone and black texture boundaries. Flesh keeps its
            // authored detail, with muted wet crimson rather than bright red.
            if(r>std::min(g,b)*1.2f && r>.08f) {
                float shade=std::clamp(r*.48f+.09f,0.f,.43f);
                c.R=uint8_t(shade*255);c.G=uint8_t(std::min(g*.6f,.068f)*255);c.B=uint8_t(std::min(b*.6f,.042f)*255);
            }
            (*palette)[i]=c;
        }
        found=woundPalettes.emplace(info.Texture,std::move(palette)).first;++stats.wounds;
    }
    info.Palette=found->second->data();info.CacheID=uint64_t(uintptr_t(found->second.get()));
}

void Draw(VisibleFrame* frame) {
    if(!allowed || !Enabled.load() || frame->ShadowView || frame->PortalDepth || frame->MirrorFlag)return;
    stats.drawn=0;
    auto texture=StainTexture();
    const vec3 crimson=RuneBloodPresentation::BloodColor();
    std::vector<std::pair<float,const Stain*>> visible;
    for(const auto& stain:stains)if(stain.live) {
        vec3 d=stain.point-frame->ViewLocation.xyz();float distance=dot(d,d);
        if(distance<1400.f*1400.f && dot(d,frame->ViewRotation.XAxis)>0)visible.emplace_back(distance,&stain);
    }
    std::sort(visible.begin(),visible.end(),[](auto a,auto b){return a.first<b.first;});
    int count=0;
    for(const auto& entry:visible) {
        if(count++>=48 || frame->EffectQuadBudget<=0)break;
        auto& s=*entry.second;vec3 u=GorePhysics::Tangent(s.normal),v=cross(s.normal,u);
        float growth=s.pool?.5f+.5f*std::clamp(float(now-s.born)/2.f,0.f,1.f):1;
        RuneEffects::Quad(frame,texture,s.point,u*s.radius*growth,v*s.radius*growth*.75f,crimson,s.pool?.8f:.7f,PF_Highlighted);
    }
    // Small real 3D fragments: eight faces, no new actor or shadow passes.
    const int faces[8][3]={{4,0,2},{4,2,1},{4,1,3},{4,3,0},{5,2,0},{5,1,2},{5,3,1},{5,0,3}};
    static const uint8_t color[4]={uint8_t(RuneBloodPresentation::BloodRed*255+.5f),uint8_t(RuneBloodPresentation::BloodGreen*255+.5f),uint8_t(RuneBloodPresentation::BloodBlue*255+.5f),255};
    static UnrealMipmap mip;static bool initialized=false;
    if(!initialized){mip.Width=mip.Height=1;mip.Data.assign(color,color+4);initialized=true;}
    TextureInfo flesh;flesh.CacheID=uint64_t(uintptr_t(&mip));flesh.Format=TextureFormat::RGBA8_;flesh.Mips=&mip;flesh.NumMips=1;flesh.WetGore=true;
    for(auto& f:fragments)if(f.body.live && frame->EffectQuadBudget>=4) {
        auto& b=f.body;if(length(b.position-frame->ViewLocation.xyz())>1200)continue;
        auto rot=mat4::rotate(b.angles.x,1,0,0)*mat4::rotate(b.angles.y,0,1,0)*mat4::rotate(b.angles.z,0,0,1);
        vec3 vertices[6]={{b.radius*f.shape.x,0,0},{-b.radius*f.shape.x,0,0},{0,b.radius*f.shape.y,0},{0,-b.radius*f.shape.y,0},{0,0,b.radius*f.shape.z},{0,0,-b.radius*f.shape.z}};
        for(auto& v:vertices)v=b.position+(rot*vec4(v,0)).xyz();
        if(b.sleeping) {
            float bottom=1e20f;for(auto v:vertices)bottom=std::min(bottom,dot(v-b.position,b.restNormal));
            for(auto& v:vertices)v-=b.restNormal*(bottom+b.radius);
        }
        if(!b.sleeping && b.age<1.3f && frame->EffectQuadBudget>5) {
            vec3 streak=frame->ViewRotation.YAxis*dot(b.velocity,frame->ViewRotation.YAxis)+frame->ViewRotation.ZAxis*dot(b.velocity,frame->ViewRotation.ZAxis);
            if(dot(streak,streak)>100) {
                streak=normalize(streak);vec3 side=cross(frame->ViewRotation.XAxis,streak);
                auto bloodTexture=RuneEffects::Texture(0,false);
                RuneEffects::Quad(frame,bloodTexture,b.position-b.velocity*.016f,side*b.radius*.22f,streak*std::min(4.f,length(b.velocity)*.008f),crimson,.55f,PF_Highlighted);
            }
        }
        float fade=std::clamp(7-b.age,0.f,1.f);
        for(auto& face:faces) {
            GouraudVertex v[3];vec3 normal=normalize(cross(vertices[face[1]]-vertices[face[0]],vertices[face[2]]-vertices[face[0]]));
            for(int i=0;i<3;++i) {
                v[i].Point=vertices[face[i]];v[i].Normal=normal;v[i].UV=vec2(64,64);v[i].Fog=vec4(0);v[i].Opacity=fade;
                v[i].Light=f.lighting.GetVertexLight(v[i].Point,normal,false,false);v[i].Lighting=&f.lighting;
            }
            // The source white sprite center supplies an opaque material;
            // vertex color is retained by the regular mesh-lighting shader.
            frame->Device->DrawGouraudPolygon(&frame->Frame,flesh,v,3,fade<1?PF_Highlighted:PF_Occlude);
        }
        frame->EffectQuadBudget-=4;++stats.drawn;
    }
}
}
