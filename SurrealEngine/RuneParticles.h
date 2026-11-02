#pragma once

#include "Engine.h"
#include "RuneParticleProfile.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UStruct.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/Random.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

// Rune's particle payload is a reflected script struct. Keep it in the original
// ParticleArray property: script callbacks and package saves see the same data.
// No native mirror of the x86 ABI, or retained pointers across map loads.
namespace RuneParticles
{
// Intern fixed property names once; per-particle NameString construction was
// a measured CPU hotspot. These retain name IDs only, never package pointers.
enum class Property { AlphaEnd, AlphaStart, BeamTextureScale, BeamThickness, ConnectionOffset, ConnectionPoint, CurrentDelay, CurrentTime, GravityScale, HasValidCoords, IsLoaded, LastTime, LifeSpanMax, LifeSpanMin, NumConPts, OldParticleCount, OriginOffset, ParticleCount, ParticleTexture, ParticleType, RandomDelay, ScaleDeltaX, ScaleDeltaY, ScaleMax, ScaleMin, ShapeVector, SpawnDelay, SpawnShape, SystemCoords, SystemLifeSpan, Target, TargetJointIndex, TargetLocation, VelocityMax, VelocityMin, ZoneVelocityScale, bAlphaFade, bApplyGravity, bApplyZoneVelocity, bConstrainToBounds, bConvergeX, bConvergeY, bConvergeZ, bEventParticleTick, bEventSystemInit, bEventSystemTick, bOneShot, bRelativeToSystem, bSystemOneShot, bTaperEndPoint, bTaperStartPoint, bUseTargetLocation, bWaterOnly };
inline const NameString& PropertyName(Property key)
{
    static const NameString names[] = { "AlphaEnd", "AlphaStart", "BeamTextureScale", "BeamThickness", "ConnectionOffset", "ConnectionPoint", "CurrentDelay", "CurrentTime", "GravityScale", "HasValidCoords", "IsLoaded", "LastTime", "LifeSpanMax", "LifeSpanMin", "NumConPts", "OldParticleCount", "OriginOffset", "ParticleCount", "ParticleTexture", "ParticleType", "RandomDelay", "ScaleDeltaX", "ScaleDeltaY", "ScaleMax", "ScaleMin", "ShapeVector", "SpawnDelay", "SpawnShape", "SystemCoords", "SystemLifeSpan", "Target", "TargetJointIndex", "TargetLocation", "VelocityMax", "VelocityMin", "ZoneVelocityScale", "bAlphaFade", "bApplyGravity", "bApplyZoneVelocity", "bConstrainToBounds", "bConvergeX", "bConvergeY", "bConvergeZ", "bEventParticleTick", "bEventSystemInit", "bEventSystemTick", "bOneShot", "bRelativeToSystem", "bSystemOneShot", "bTaperEndPoint", "bTaperStartPoint", "bUseTargetLocation", "bWaterOnly" };
    return names[static_cast<size_t>(key)];
}

inline bool IsSystem(UActor* actor)
{
    static const NameString particleSystem("ParticleSystem");
    return engine->LaunchInfo.IsRune() && actor && actor->IsA(particleSystem);
}

enum class Field { Location, Velocity, LifeSpan, XScale, YScale, ScaleStartX, ScaleStartY, Alpha, U0, V0, U1, V1, Points, Style, TextureIndex, Valid, Count };

class ArrayView
{
public:
    explicit ArrayView(UActor* actor)
    {
        static const NameString arrayName("ParticleArray");
        auto* array = UObject::Cast<UStructProperty>(actor->GetMemberProperty(arrayName));
        data = static_cast<char*>(actor->GetProperty(array));
        stride = array->ElementSize();
        structure = array->Struct;
    }
    template<class T> T& Ref(int index, Field member, int element = 0) const
    {
        const auto& field = Resolve(member);
        return *reinterpret_cast<T*>(data + index * stride + field.Offset + element * field.Stride);
    }
    bool Valid(int index) const
    {
        const auto& field = Resolve(Field::Valid);
        return (Ref<uint32_t>(index, Field::Valid) & field.Mask) != 0;
    }
    void SetValid(int index, bool value) const
    {
        const auto& field = Resolve(Field::Valid);
        auto& bits = Ref<uint32_t>(index, Field::Valid);
        bits = value ? (bits | field.Mask) : (bits & ~field.Mask);
    }
private:
    struct Layout { bool Resolved = false; size_t Offset = 0, Stride = 0; uint32_t Mask = 0; };
    const Layout& Resolve(Field member) const
    {
        // Resolve once per view, rather than interning strings and looking up
        // reflection metadata for every member of every particle. These small
        // caches die with the view, so they cannot outlive a loaded package.
        static const NameString names[] = { "Location", "Velocity", "LifeSpan", "XScale", "YScale", "ScaleStartX", "ScaleStartY", "Alpha", "U0", "V0", "U1", "V1", "Points", "Style", "TextureIndex", "Valid" };
        auto& field = fields[static_cast<size_t>(member)];
        if (!field.Resolved)
        {
            auto* property = structure->FindMemberProperty(names[static_cast<size_t>(member)]);
            if (!property) throw std::runtime_error("Missing Rune particle field");
            field.Offset = property->DataOffset.DataOffset;
            field.Stride = property->ElementSize();
            field.Mask = property->DataOffset.BitfieldMask;
            field.Resolved = true;
        }
        return field;
    }
    UStruct* structure;
    char* data;
    size_t stride;
    mutable Layout fields[static_cast<size_t>(Field::Count)] = {};
};

inline void Unsupported(UActor* actor, const char* feature)
{
    // Class names, not actor pointers: bounded diagnostics survive GC safely.
    static std::set<std::string> reported;
    std::string key = actor->Class->Name.ToString() + ":" + feature;
    if (reported.insert(key).second)
        std::cout << "RUNE_PARTICLE_UNSUPPORTED class=" << actor->Class->Name.ToString() << " feature=" << feature << '\n';
}

inline vec3 Rotate(UActor* actor, const vec3& vector)
{
    return (Coords::Rotation(actor->Rotation()).ToMatrix() * vec4(vector,0.0f)).xyz();
}

inline void Include(BBox& box, const vec3& point, float radius = 0.0f)
{
    for (int axis=0;axis<3;axis++)
    {
        box.min[axis]=std::min(box.min[axis],point[axis]-radius);
        box.max[axis]=std::max(box.max[axis],point[axis]+radius);
    }
}

inline vec3 BeamTarget(UActor* actor, bool joint = true)
{
    if (actor->GetBool(PropertyName(Property::bUseTargetLocation))) return actor->GetVector(PropertyName(Property::TargetLocation));
    auto* target=UObject::TryCast<UActor>(actor->GetUObject(PropertyName(Property::Target)));
    if (!target || target->bDeleteMe()) return actor->Location();
    int index=static_cast<int>(actor->GetInt(PropertyName(Property::TargetJointIndex)));
    if (joint && index!=0)
    {
        ExpressionValue args[] = {ExpressionValue::IntValue(index)};
        return Frame::Call(FindEventFunction(target,"GetJointPos"),target,{args,1}).ToVector();
    }
    return target->Location();
}

inline BBox Bounds(UActor* actor)
{
    RuneParticleProfile::Scope profile(RuneParticleProfile::Bounds);
    BBox box(actor->Location(),actor->Location());
    if (actor->GetByte(PropertyName(Property::ParticleType))==3)
    {
        float width=std::max(1.0f,std::abs(actor->GetFloat(PropertyName(Property::BeamThickness))));
        Include(box,actor->Location()+actor->GetVector(PropertyName(Property::OriginOffset)),width);
        Include(box,BeamTarget(actor,false),width+128.0f);
        auto* points=static_cast<vec3*>(actor->GetProperty(PropertyName(Property::ConnectionPoint)));
        if(actor->GetBool(PropertyName(Property::IsLoaded)))
            for(int i=0;i<std::min(32,(int)actor->GetByte(PropertyName(Property::NumConPts)));i++) Include(box,points[i],width);
        return box;
    }
    // Include the possible spawn volume before render-driven initialization.
    // Otherwise a previously unseen emitter would be culled with a zero box.
    float size=1.0f;
    auto* textures=static_cast<UTexture**>(actor->GetProperty(PropertyName(Property::ParticleTexture)));
    for(int i=0;i<4;i++) if(textures[i]) size=std::max(size,(float)std::max(textures[i]->USize(),textures[i]->VSize()));
    float scale=std::max(std::abs(actor->GetFloat(PropertyName(Property::ScaleMin))),std::abs(actor->GetFloat(PropertyName(Property::ScaleMax))));
    float radius=length(actor->GetVector(PropertyName(Property::ShapeVector)))+size*scale;
    Include(box,actor->Location()+actor->GetVector(PropertyName(Property::OriginOffset)),radius);
    if(actor->GetBool(PropertyName(Property::IsLoaded)))
    {
        ArrayView particles(actor);
        for(int i=0;i<std::min(64,(int)actor->GetByte(PropertyName(Property::ParticleCount)));i++) if(particles.Valid(i))
        {
            float pscale=std::max(std::abs(particles.Ref<float>(i,RuneParticles::Field::XScale)),std::abs(particles.Ref<float>(i,RuneParticles::Field::YScale)));
            Include(box,particles.Ref<vec3>(i,RuneParticles::Field::Location),size*pscale);
        }
    }
    return box;
}

inline void InitBeam(UActor* actor, ArrayView& particles)
{
    // The original BeamSystem.uc retains this native initializer verbatim as
    // a commented reference. Each connection uses three strips of the texture.
    int count=std::min(64,(int)actor->GetByte(PropertyName(Property::ParticleCount)));
    float alpha=actor->GetByte(PropertyName(Property::AlphaStart))/255.0f;
    for(int i=0;i<count;i++)
    {
        particles.SetValid(i,true);
        particles.Ref<uint8_t>(i,RuneParticles::Field::Style)=actor->Style();
        particles.Ref<uint8_t>(i,RuneParticles::Field::TextureIndex)=0;
        particles.Ref<vec3>(i,RuneParticles::Field::Velocity)=vec3(0);
        particles.Ref<vec3>(i,RuneParticles::Field::Alpha)=vec3(alpha);
        particles.Ref<vec3>(i,RuneParticles::Field::Location)=actor->Location();
        particles.Ref<float>(i,RuneParticles::Field::LifeSpan)=actor->GetFloat(PropertyName(Property::LifeSpanMax));
        particles.Ref<float>(i,RuneParticles::Field::XScale)=particles.Ref<float>(i,RuneParticles::Field::YScale)=1.0f;
        particles.Ref<float>(i,RuneParticles::Field::U0)=(i%3)*0.33f;
        particles.Ref<float>(i,RuneParticles::Field::U1)=(i%3==2) ? 1.0f : (i%3+1)*0.33f;
        particles.Ref<float>(i,RuneParticles::Field::V0)=0.0f;
        particles.Ref<float>(i,RuneParticles::Field::V1)=1.0f;
    }
    actor->SetBool(PropertyName(Property::IsLoaded),true);
}

inline void UpdateBeamPoints(UActor* actor)
{
    int count=std::min(32,(int)actor->GetByte(PropertyName(Property::NumConPts)));
    if(count<2) return;
    vec3 start=actor->Location()+actor->GetVector(PropertyName(Property::OriginOffset));
    if(!actor->GetBool(PropertyName(Property::bUseTargetLocation)) && !actor->GetUObject(PropertyName(Property::Target))) return;
    vec3 end=BeamTarget(actor);
    auto* points=static_cast<vec3*>(actor->GetProperty(PropertyName(Property::ConnectionPoint)));
    auto* offsets=static_cast<vec3*>(actor->GetProperty(PropertyName(Property::ConnectionOffset)));
    for(int i=0;i<count;i++) points[i]=start+(end-start)*(float(i)/float(count-1))+offsets[i];
}

inline void UpdateBeamQuads(UActor* actor, ArrayView& particles)
{
    int count=std::min({32,(int)actor->GetByte(PropertyName(Property::NumConPts)),1+(int)actor->GetByte(PropertyName(Property::ParticleCount))/3});
    if(count<2) return;
    auto* points=static_cast<vec3*>(actor->GetProperty(PropertyName(Property::ConnectionPoint)));
    vec3 axis=points[count-1]-points[0];
    if(dot(axis,axis)<0.000001f) return;
    axis=normalize(axis);
    vec3 side=normalize(cross(axis,std::abs(axis.z)<0.9f ? vec3(0,0,1) : vec3(0,1,0)));
    vec3 up=cross(side,axis);
    float width=actor->GetFloat(PropertyName(Property::BeamThickness));
    // Three longitudinal faces, as in the original native beam. The retained
    // Points array also supports scripted ParticleTick replacements.
    vec3 triangle[3]={up*(width/1.7320508f), side*(width*0.75f)-up*(width/1.7320508f),-side*(width*0.75f)-up*(width/1.7320508f)};
    float texScale=actor->GetFloat(PropertyName(Property::BeamTextureScale));
    for(int i=0;i<count-1;i++) for(int face=0;face<3;face++)
    {
        int p=i*3+face;
        vec3 start=triangle[face], startNext=triangle[(face+1)%3];
        vec3 end=start, endNext=startNext;
        if(i==0 && actor->GetBool(PropertyName(Property::bTaperStartPoint))) start=startNext=vec3(0);
        if(i==count-2 && actor->GetBool(PropertyName(Property::bTaperEndPoint))) end=endNext=vec3(0);
        particles.Ref<vec3>(p,RuneParticles::Field::Points,0)=points[i]+start;
        particles.Ref<vec3>(p,RuneParticles::Field::Points,1)=points[i]+startNext;
        particles.Ref<vec3>(p,RuneParticles::Field::Points,2)=points[i+1]+endNext;
        particles.Ref<vec3>(p,RuneParticles::Field::Points,3)=points[i+1]+end;
        particles.Ref<vec3>(p,RuneParticles::Field::Location)=points[i];
        if(texScale>0)
        {
            particles.Ref<float>(p,RuneParticles::Field::V0)=length(points[i]-points[0])*texScale;
            particles.Ref<float>(p,RuneParticles::Field::V1)=length(points[i+1]-points[0])*texScale;
        }
    }
}

inline void SpawnEmitter(UActor* actor, ArrayView& particles, int index)
{
    vec3 shape=actor->GetVector(PropertyName(Property::ShapeVector));
    vec3 offset=vec3(FRandRange(-1,1),FRandRange(-1,1),FRandRange(-1,1))*shape;
    // Native PSHAPE_Ellipsoid uses independent component ranges (a box).
    // PSHAPE_Line rotates that volume into the emitter's local axes.
    if(actor->GetByte(PropertyName(Property::SpawnShape))==1) offset=Rotate(actor,offset);
    else if(actor->GetByte(PropertyName(Property::SpawnShape))!=0) Unsupported(actor,"spawn-shape");
    vec3 origin=actor->Location()+actor->GetVector(PropertyName(Property::OriginOffset));
    vec3& position=particles.Ref<vec3>(index,RuneParticles::Field::Location);
    position=origin+offset;
    if(actor->GetBool(PropertyName(Property::HasValidCoords)))
    {
        // Engine 103a25e3..103a263c transforms the already assembled spawn
        // position, then recomputes its displacement from the emitter origin
        // for the convergence branches. Core's TransformPointBy subtracts
        // Coords.Origin and dots with its three axes (not a matrix inverse).
        const auto& coords=*static_cast<Coords*>(actor->GetProperty(PropertyName(Property::SystemCoords)));
        position=coords*position;
        offset=position-origin;
    }
    vec3 vmin=actor->GetVector(PropertyName(Property::VelocityMin)), vmax=actor->GetVector(PropertyName(Property::VelocityMax));
    vec3 velocity=vec3(FRandRange(vmin.x,vmax.x),FRandRange(vmin.y,vmax.y),FRandRange(vmin.z,vmax.z));
    if(actor->bDirectional()) velocity=Rotate(actor,velocity);
    if(actor->GetBool(PropertyName(Property::bConvergeX))) velocity.x=-offset.x*FRandRange(vmin.x,vmax.x);
    if(actor->GetBool(PropertyName(Property::bConvergeY))) velocity.y=-offset.y*FRandRange(vmin.y,vmax.y);
    if(actor->GetBool(PropertyName(Property::bConvergeZ))) velocity.z=-offset.z*FRandRange(vmin.z,vmax.z);
    particles.Ref<vec3>(index,RuneParticles::Field::Velocity)=velocity;
    float scale=FRandRange(actor->GetFloat(PropertyName(Property::ScaleMin)),actor->GetFloat(PropertyName(Property::ScaleMax)));
    particles.Ref<float>(index,RuneParticles::Field::XScale)=particles.Ref<float>(index,RuneParticles::Field::YScale)=scale;
    particles.Ref<float>(index,RuneParticles::Field::ScaleStartX)=particles.Ref<float>(index,RuneParticles::Field::ScaleStartY)=scale;
    particles.Ref<vec3>(index,RuneParticles::Field::Alpha)=vec3(actor->GetByte(PropertyName(Property::AlphaStart))/255.0f);
    particles.Ref<float>(index,RuneParticles::Field::LifeSpan)=FRandRange(actor->GetFloat(PropertyName(Property::LifeSpanMin)),actor->GetFloat(PropertyName(Property::LifeSpanMax)));
    // The shipped native unconditionally assigns zero after its random branch.
    particles.Ref<uint8_t>(index,RuneParticles::Field::TextureIndex)=0;
    particles.Ref<uint8_t>(index,RuneParticles::Field::Style)=actor->Style();
    particles.SetValid(index,true);
    if(actor->GetBool(PropertyName(Property::bRelativeToSystem))) Unsupported(actor,"relative-emitter-transform");
}

inline void InitEmitter(UActor* actor, ArrayView& particles)
{
    int count=std::min(64,(int)actor->GetByte(PropertyName(Property::ParticleCount)));
    if(actor->GetFloat(PropertyName(Property::SpawnDelay))>0)
    {
        // Original delayed initialization discards the one-time coordinate
        // transform before marking its particles invalid. Immediate init
        // keeps it for the whole initial batch, then clears it below.
        actor->SetBool(PropertyName(Property::IsLoaded),true);
        actor->SetBool(PropertyName(Property::HasValidCoords),false);
    }
    for(int i=0;i<count;i++)
    {
        if(actor->GetFloat(PropertyName(Property::SpawnDelay))>0) particles.SetValid(i,false);
        else SpawnEmitter(actor,particles,i);
    }
    actor->SetBool(PropertyName(Property::IsLoaded),true);
    actor->SetBool(PropertyName(Property::HasValidCoords),false);
    actor->SetByte(PropertyName(Property::OldParticleCount),count);
    float delay=actor->GetFloat(PropertyName(Property::RandomDelay));
    if(delay!=0) actor->SetFloat(PropertyName(Property::CurrentDelay),FRandRange(delay,2.0f*delay));
}

inline void EmitterSystemTick(UActor* actor, ArrayView& particles, float elapsed)
{
    int count=std::min(64,(int)actor->GetByte(PropertyName(Property::ParticleCount)));
    float delay=actor->GetFloat(PropertyName(Property::SpawnDelay));
    if(delay>0)
    {
        delay-=elapsed;
        actor->SetFloat(PropertyName(Property::SpawnDelay),delay);
        if(delay<=0) for(int i=0;i<count;i++) SpawnEmitter(actor,particles,i);
        return;
    }
    if(actor->GetFloat(PropertyName(Property::RandomDelay))!=0)
    {
        float next=actor->GetFloat(PropertyName(Property::CurrentDelay))-elapsed;
        actor->SetFloat(PropertyName(Property::CurrentDelay),next);
        if(next<=0) InitEmitter(actor,particles);
    }
    if(actor->GetBool(PropertyName(Property::bSystemOneShot)))
    {
        bool alive=false;
        for(int i=0;i<count;i++) alive|=particles.Valid(i);
        if(!alive) {if(actor->NetworkReplica)actor->bHidden()=true;else actor->Destroy();}
        return;
    }
    float life=actor->GetFloat(PropertyName(Property::SystemLifeSpan));
    if(life>0)
    {
        life-=elapsed;
        actor->SetFloat(PropertyName(Property::SystemLifeSpan),life);
        if(life<=0) {if(actor->NetworkReplica)actor->bHidden()=true;else actor->Destroy();}
    }
}

inline void UpdateEmitter(UActor* actor, ArrayView& particles, float elapsed)
{
    int count=std::min(64,(int)actor->GetByte(PropertyName(Property::ParticleCount)));
    float maxLife=actor->GetFloat(PropertyName(Property::LifeSpanMax));
    UZoneInfo* zone=actor->Region().Zone;
    const bool fade=actor->GetBool(PropertyName(Property::bAlphaFade)), gravity=actor->GetBool(PropertyName(Property::bApplyGravity)),
        zoneVelocity=actor->GetBool(PropertyName(Property::bApplyZoneVelocity)), constrained=actor->GetBool(PropertyName(Property::bConstrainToBounds)),
        waterOnly=actor->GetBool(PropertyName(Property::bWaterOnly)), oneShot=actor->GetBool(PropertyName(Property::bOneShot));
    const float alphaStart=actor->GetByte(PropertyName(Property::AlphaStart)), alphaEnd=actor->GetByte(PropertyName(Property::AlphaEnd)),
        dx=actor->GetFloat(PropertyName(Property::ScaleDeltaX)), dy=actor->GetFloat(PropertyName(Property::ScaleDeltaY));
    vec3 acceleration(0);
    if(zone && gravity) acceleration+=zone->ZoneGravity()*actor->GetFloat(PropertyName(Property::GravityScale));
    if(zone && zoneVelocity) acceleration+=zone->ZoneVelocity()*actor->GetFloat(PropertyName(Property::ZoneVelocityScale));
    const vec3 origin=actor->Location()+actor->GetVector(PropertyName(Property::OriginOffset));
    const float radiusSquared=actor->CollisionRadius()*actor->CollisionRadius(), height=actor->CollisionHeight();
    for(int i=0;i<count;i++) if(particles.Valid(i))
    {
        float& life=particles.Ref<float>(i,RuneParticles::Field::LifeSpan);
        float fraction=maxLife!=0 ? 1.0f-life/maxLife : 0.0f;
        if(fade)
            particles.Ref<vec3>(i,RuneParticles::Field::Alpha)=vec3((alphaStart+(alphaEnd-alphaStart)*fraction)/255.0f);
        if(dx!=0) particles.Ref<float>(i,RuneParticles::Field::XScale)=particles.Ref<float>(i,RuneParticles::Field::ScaleStartX)*(1+(dx-1)*fraction);
        if(dy!=0) particles.Ref<float>(i,RuneParticles::Field::YScale)=particles.Ref<float>(i,RuneParticles::Field::ScaleStartY)*(1+(dy-1)*fraction);
        vec3& position=particles.Ref<vec3>(i,RuneParticles::Field::Location);
        vec3& velocity=particles.Ref<vec3>(i,RuneParticles::Field::Velocity);
        position+=velocity*elapsed;
        velocity+=acceleration*elapsed;
        bool expired=false;
        if(life>0) { life-=elapsed; expired=life<=0; }
        if(constrained)
        {
            vec3 p=position-origin;
            expired|=std::abs(p.z)>height || p.x*p.x+p.y*p.y>radiusSquared;
        }
        if(waterOnly && zone && !zone->bWaterZone()) expired=true;
        if(expired)
        {
            if(oneShot) particles.SetValid(i,false);
            else SpawnEmitter(actor,particles,i);
        }
    }
}

inline void Tick(UActor* actor, float elapsed)
{
    // Original AActor::Tick omits native particle updates while bHidden;
    // ordinary script/state/timer updates still run outside this function.
    if(!IsSystem(actor) || actor->bDeleteMe() || actor->bHidden() || !std::isfinite(elapsed) || elapsed<0) return;
    RuneParticleProfile::Scope profile(RuneParticleProfile::Tick);
    float time=actor->GetFloat(PropertyName(Property::CurrentTime))+elapsed;
    actor->SetFloat(PropertyName(Property::CurrentTime),time);
    actor->SetFloat(PropertyName(Property::LastTime),time);
    if(actor->GetByte(PropertyName(Property::ParticleCount))>64) actor->SetByte(PropertyName(Property::ParticleCount),64);
    if(!actor->GetByte(PropertyName(Property::ParticleCount))) { actor->SetByte(PropertyName(Property::OldParticleCount),0); return; }
    int type=actor->GetByte(PropertyName(Property::ParticleType));
    ArrayView particles(actor);
    if(!actor->GetBool(PropertyName(Property::IsLoaded)))
    {
        if(actor->GetBool(PropertyName(Property::bEventSystemInit))) CallEvent(actor,"SystemInit");
        else if(type==0) InitEmitter(actor,particles);
        else if(type==3) InitBeam(actor,particles);
        else { Unsupported(actor,type==1 ? "swipe-system" : "generic-system"); return; }
    }
    else if(type==0)
    {
        // Spewer scripts ramp ParticleCount at runtime. Initialize new slots
        // without resetting live particles or re-running SystemInit callbacks.
        int previous=std::min(64,(int)actor->GetByte(PropertyName(Property::OldParticleCount)));
        int current=actor->GetByte(PropertyName(Property::ParticleCount));
        if(!actor->GetBool(PropertyName(Property::bEventSystemInit)) && actor->GetFloat(PropertyName(Property::SpawnDelay))<=0)
            for(int i=previous;i<current;i++) SpawnEmitter(actor,particles,i);
        actor->SetByte(PropertyName(Property::OldParticleCount),current);
    }
    if(actor->bDeleteMe() || (engine->LevelInfo && !engine->LevelInfo->Pauser().empty())) return;
    if(actor->GetBool(PropertyName(Property::bEventSystemTick))) CallEvent(actor,"SystemTick",{ExpressionValue::FloatValue(elapsed)});
    else if(type==0) EmitterSystemTick(actor,particles,elapsed);
    if(actor->bDeleteMe() || actor->bHidden()) return;
    if(actor->GetBool(PropertyName(Property::bEventParticleTick))) CallEvent(actor,"ParticleTick",{ExpressionValue::FloatValue(elapsed)});
    else if(type==0) UpdateEmitter(actor,particles,elapsed);
    else if(type==3)
    {
        UpdateBeamPoints(actor);
        UpdateBeamQuads(actor,particles);
        float maxLife=actor->GetFloat(PropertyName(Property::LifeSpanMax));
        for(int i=0;i<actor->GetByte(PropertyName(Property::ParticleCount));i++) if(particles.Valid(i))
        {
            float& life=particles.Ref<float>(i,RuneParticles::Field::LifeSpan);
            life=std::max(0.0f,life-elapsed);
            if(actor->GetBool(PropertyName(Property::bAlphaFade)) && maxLife!=0)
                particles.Ref<vec3>(i,RuneParticles::Field::Alpha)=vec3((actor->GetByte(PropertyName(Property::AlphaStart))+(actor->GetByte(PropertyName(Property::AlphaEnd))-actor->GetByte(PropertyName(Property::AlphaStart)))*(1-life/maxLife))/255.0f);
        }
    }
    else Unsupported(actor,type==1 ? "swipe-particle-update" : "generic-particle-update");
}
}
