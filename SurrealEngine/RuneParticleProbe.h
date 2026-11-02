#pragma once

#include "RuneParticles.h"
#include "RuneBloodPresentation.h"
#include "Render/VisibleRuneBlood.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include <stdexcept>

inline void RunRuneParticleProbe(Engine& game,ULevel* level)
{
    auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    auto nearValue=[](float a,float b) { return std::abs(a-b)<0.0001f; };
    auto* info=UObject::Cast<ULevelInfo>(level->Actors[0]);
    USurrealAudioDevice silent("RuneParticleProbeAudio",nullptr,ObjectFlags::Transient);
    struct Context
    {
        Engine& Game; ULevel* Level; ULevelInfo* Info; USurrealAudioDevice* Audio;
        ULevelInfo* FixtureInfo; bool Begun; size_t ActorCount; std::string Pauser;
        ~Context()
        {
            Game.Level->Actors.resize(ActorCount);
            FixtureInfo->bBegunPlay()=Begun; FixtureInfo->Pauser()=Pauser;
            Game.Level=Level; Game.LevelInfo=Info; Game.audiodev=Audio;
        }
    } context{game,game.Level,game.LevelInfo,game.audiodev,info,bool(info->bBegunPlay()),level->Actors.size(),info->Pauser()};
    game.Level=level; game.LevelInfo=info; game.audiodev=&silent;
    info->bBegunPlay()=true; info->Pauser().clear();
    using RuneBloodPresentation::Wound;
    Wound wound;
    require(!wound.Observe(100,0,true),"Joining must not start bleeding");
    require(!wound.Observe(90,.1,true),"Minor hit must not start a trail");
    require(!wound.Observe(55,.2,false),"Health loss without red impact must not bleed");
    require(wound.Observe(55,.25,true),"Delayed red impact must correlate with heavy damage");
    require(wound.Emit(.25) && !wound.Emit(.26) && wound.Emit(.38),"Droplet cadence must be bounded");
    require(!wound.Emit(2.26),"Bleeding must expire");
    require(!wound.Observe(100,3,true),"Healing must not create blood");
    require(!wound.Observe(0,4,false),"No bloodless death trail");
    require(!wound.Observe(100,4.1,true) && !wound.Emit(4.1),"Respawn must clear old wounds");
    auto border=RuneBloodPresentation::Crimson(TextureColor(128,128,128,255));
    auto core=RuneBloodPresentation::Crimson(TextureColor(80,10,5,255));
    require(border.A==0 && border.R==0,"Neutral modulated border must become transparent");
    require(core.R>core.G*5 && core.R<=core.A && core.A>150,"Blood must be premultiplied crimson");
    std::cout<<"RUNE_BLOOD_POLICY_OK threshold=25 duration=2 cadence=.12 heal_respawn_reset=1 neutral_border=1\n";
    RuneBloodPresentation::Reset();
    for(int i=0;i<300;++i) {
        RuneBloodPresentation::Drop d;d.p=vec3(float(i),0,0);d.radius=2;RuneBloodPresentation::Land(d);
    }
    require(RuneBloodPresentation::stainCursor==300,"Stain admission count");
    for(const auto& stain:RuneBloodPresentation::stains)require(stain.live && stain.p.x>=44 && stain.p.x<300,"Stain history must evict oldest landings only");
    RuneBloodPresentation::ClearTransient();
    require(std::all_of(RuneBloodPresentation::stains.begin(),RuneBloodPresentation::stains.end(),[](const auto& s){return s.live;}),"Observation gaps must retain floor stains");
    RuneBloodPresentation::Reset();
    require(std::none_of(RuneBloodPresentation::stains.begin(),RuneBloodPresentation::stains.end(),[](const auto& s){return s.live;}) && RuneBloodPresentation::stainCursor==0,"Map reset must clear stain history");
    std::cout<<"RUNE_BLOOD_PERSISTENCE_OK capacity=256 oldest_evicted=44 gaps_preserve=1 map_clear=1\n";
    int serial=0;
    auto make=[&](const char* package,const char* cls)
    {
        auto* actor=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject(
            "RuneParticleFixture"+std::to_string(++serial),game.packages->GetPackage(package)->GetClass(cls),ObjectFlags::Transient));
        actor->XLevel()=level; actor->Level()=info; actor->Region().Zone=info;
        actor->Index=(int)level->Actors.size(); level->Actors.push_back(actor);
        actor->bStatic()=false; actor->bNoDelete()=false;
        actor->SetBool("bSystemOneShot",false); actor->SetBool("bOneShot",false);
        actor->SetBool("bEventSystemInit",false); actor->SetBool("bEventSystemTick",false); actor->SetBool("bEventParticleTick",false);
        actor->SetBool("bApplyGravity",false); actor->SetBool("bApplyZoneVelocity",false);
        actor->SetBool("bWaterOnly",false); actor->SetBool("bConstrainToBounds",false);
        actor->SetBool("bRelativeToSystem",false); actor->SetBool("HasValidCoords",false);
        actor->SetBool("bConvergeX",false); actor->SetBool("bConvergeY",false); actor->SetBool("bConvergeZ",false);
        actor->SetBool("IsLoaded",false); actor->SetByte("ParticleCount",2);
        actor->SetFloat("SpawnDelay",0); actor->SetFloat("RandomDelay",0); actor->SetFloat("SystemLifeSpan",0);
        actor->SetVector("ShapeVector",vec3(0)); actor->SetVector("OriginOffset",vec3(0));
        actor->SetVector("VelocityMin",vec3(10,20,30)); actor->SetVector("VelocityMax",vec3(10,20,30));
        actor->SetFloat("LifeSpanMin",2); actor->SetFloat("LifeSpanMax",2);
        actor->SetFloat("ScaleMin",1); actor->SetFloat("ScaleMax",1);
        actor->SetFloat("ScaleDeltaX",2); actor->SetFloat("ScaleDeltaY",0);
        actor->SetByte("AlphaStart",255); actor->SetByte("AlphaEnd",0); actor->SetBool("bAlphaFade",true);
        actor->bDirectional()=false; actor->Location()=vec3(100,200,300);
        return actor;
    };
    auto* emitter=make("Engine","ParticleSystem"); emitter->SetByte("ParticleType",0);
    RuneParticles::Tick(emitter,0);
    RuneParticles::ArrayView p(emitter);
    require(p.Valid(0) && p.Valid(1),"Emitter did not initialize reflected ParticleArray");
    RuneParticles::Tick(emitter,0.5f); RuneParticles::Tick(emitter,0.5f);
    require(length(p.Ref<vec3>(0,RuneParticles::Field::Location)-vec3(110,220,330))<0.001f,"Emitter velocity integration failed");
    require(nearValue(p.Ref<float>(0,RuneParticles::Field::LifeSpan),1) && nearValue(p.Ref<vec3>(0,RuneParticles::Field::Alpha).x,0.75f) && nearValue(p.Ref<float>(0,RuneParticles::Field::XScale),1.25f),
        "Emitter lifespan, alpha or scale differs from native pre-decrement evolution");
    emitter->SetBool("bEventParticleTick",true); // Original base callback is empty.
    RuneParticles::Tick(emitter,0.5f);
    require(nearValue(p.Ref<float>(0,RuneParticles::Field::LifeSpan),1),"ParticleTick callback supplemented native update instead of replacing it");
    emitter->SetBool("bEventParticleTick",false);
    float hiddenTime=emitter->GetFloat("CurrentTime"), hiddenLife=p.Ref<float>(0,RuneParticles::Field::LifeSpan);
    emitter->bHidden()=true; RuneParticles::Tick(emitter,1);
    require(emitter->GetFloat("CurrentTime")==hiddenTime && p.Ref<float>(0,RuneParticles::Field::LifeSpan)==hiddenLife,
        "Hidden native particle system advanced despite original ActorTick guard");
    emitter->bHidden()=false;
    emitter->SetFloat("SystemLifeSpan",5); emitter->SetBool("bEventSystemTick",true);
    RuneParticles::Tick(emitter,0.1f);
    require(nearValue(emitter->GetFloat("SystemLifeSpan"),5),"SystemTick callback did not replace native system tick");
    info->Pauser()="ProbePaused";
    float before=p.Ref<float>(0,RuneParticles::Field::LifeSpan); RuneParticles::Tick(emitter,1);
    require(nearValue(p.Ref<float>(0,RuneParticles::Field::LifeSpan),before),"Paused particle system advanced"); info->Pauser().clear();

    auto systemCoords=[&](UActor* actor)
    {
        actor->SetByte("ParticleType",0);
        actor->SetVector("OriginOffset",vec3(1,2,3));
        actor->SetVector("VelocityMin",vec3(2,3,4)); actor->SetVector("VelocityMax",vec3(2,3,4));
        actor->SetBool("bConvergeX",true); actor->SetBool("bConvergeY",true); actor->SetBool("bConvergeZ",true);
        actor->SetBool("HasValidCoords",true);
        auto& coords=*static_cast<Coords*>(actor->GetProperty("SystemCoords"));
        coords.Origin=vec3(10,20,30);
        coords.XAxis=vec3(0,2,0); coords.YAxis=vec3(-3,0,0); coords.ZAxis=vec3(0,0,0.5f);
    };
    auto* transformed=make("Engine","ParticleSystem"); systemCoords(transformed);
    RuneParticles::Tick(transformed,0);
    RuneParticles::ArrayView transformedParticles(transformed);
    // Independently calculated: (101,202,303)-(10,20,30)=(91,182,273),
    // dotted against the three nonidentity axes gives (364,-273,136.5).
    for(int i=0;i<2;i++)
    {
        require(length(transformedParticles.Ref<vec3>(i,RuneParticles::Field::Location)-vec3(364,-273,136.5f))<0.001f,
            "SystemCoords did not transform every initial spawn point with subtract-origin/axis-dot semantics");
        require(length(transformedParticles.Ref<vec3>(i,RuneParticles::Field::Velocity)-vec3(-526,1425,666))<0.001f,
            "Convergence did not use the transformed position relative to the untransformed emitter origin");
    }
    require(!transformed->GetBool("HasValidCoords"),"Immediate initialization retained one-time SystemCoords");
    RuneParticles::SpawnEmitter(transformed,transformedParticles,0);
    require(length(transformedParticles.Ref<vec3>(0,RuneParticles::Field::Location)-vec3(101,202,303))<0.001f &&
        length(transformedParticles.Ref<vec3>(0,RuneParticles::Field::Velocity))<0.001f,"Respawn reused cleared SystemCoords or stale convergence offset");
    auto* delayedCoords=make("Engine","ParticleSystem"); systemCoords(delayedCoords); delayedCoords->SetFloat("SpawnDelay",0.5f);
    RuneParticles::Tick(delayedCoords,0);
    RuneParticles::ArrayView delayedCoordParticles(delayedCoords);
    require(!delayedCoords->GetBool("HasValidCoords") && !delayedCoordParticles.Valid(0),
        "Delayed initialization failed to discard SystemCoords before spawning");
    RuneParticles::Tick(delayedCoords,0.5f);
    require(delayedCoordParticles.Valid(0) && length(delayedCoordParticles.Ref<vec3>(0,RuneParticles::Field::Location)-vec3(101,202,303))<0.001f,
        "Delayed spawn incorrectly applied the discarded coordinate transform");

    auto* delayed=make("Engine","ParticleSystem"); delayed->SetByte("ParticleType",0); delayed->SetFloat("SpawnDelay",0.5f);
    RuneParticles::Tick(delayed,0.25f); RuneParticles::ArrayView d(delayed);
    require(!d.Valid(0),"Emitter ignored SpawnDelay"); RuneParticles::Tick(delayed,0.25f);
    require(d.Valid(0),"Emitter did not spawn after delay");
    delayed->SetByte("ParticleCount",4); RuneParticles::Tick(delayed,0);
    require(d.Valid(3),"Spewer ParticleCount growth left new particles invalid");
    delayed->SetBool("bOneShot",true); delayed->SetBool("bSystemOneShot",true);
    for(int i=0;i<4;i++) d.Ref<float>(i,RuneParticles::Field::LifeSpan)=0.01f;
    RuneParticles::Tick(delayed,0.02f);
    require(!d.Valid(0) && !delayed->bDeleteMe(),"One-shot invalidation did not precede system cleanup");
    RuneParticles::Tick(delayed,0.02f);
    require(delayed->bDeleteMe() && level->Actors[delayed->Index]==nullptr,"Drained one-shot particle actor leaked");
    auto* timed=make("Engine","ParticleSystem"); timed->SetByte("ParticleType",0); timed->SetFloat("SystemLifeSpan",0.01f);
    RuneParticles::Tick(timed,0.02f);
    require(timed->bDeleteMe(),"SystemLifeSpan did not destroy emitter");

    auto* beam=make("RuneI","Rope"); beam->SetByte("ParticleType",3); beam->SetByte("ParticleCount",9); beam->SetByte("NumConPts",3);
    beam->SetBool("bUseTargetLocation",true); beam->SetVector("OriginOffset",vec3(0,0,100)); beam->SetVector("TargetLocation",vec3(100,200,100));
    beam->SetFloat("BeamThickness",5); beam->SetBool("bEventSystemTick",true);
    beam->SetFloat("CurMultiplier",0); beam->SetFloat("IdleSway",0); beam->SetFloat("ElapsedTime",0);
    RuneParticles::Tick(beam,0.25f);
    auto* points=static_cast<vec3*>(beam->GetProperty("ConnectionPoint"));
    require(length(points[0]-vec3(100,200,400))<0.001f && length(points[2]-vec3(100,200,100))<0.001f,"Rope beam endpoint geometry failed");
    require(nearValue(beam->GetFloat("ElapsedTime"),0.25f),"Original Rope SystemTick callback did not execute");
    RuneParticles::ArrayView b(beam);
    require(length(b.Ref<vec3>(0,RuneParticles::Field::Points,0)-b.Ref<vec3>(0,RuneParticles::Field::Points,2))>140,"Beam did not generate a visible quad");
    auto bounds=RuneParticles::Bounds(beam);
    require(bounds.min.z<=100 && bounds.max.z>=400,"Beam bounds omit endpoints");
    auto* explosion=make("RuneI","Explosion"); explosion->SetByte("ParticleType",0); explosion->SetBool("bEventSystemInit",true);
    explosion->SetFloat("ScaleMin",99); explosion->SetFloat("ScaleMax",99); RuneParticles::Tick(explosion,0);
    RuneParticles::ArrayView e(explosion);
    require(e.Ref<float>(0,RuneParticles::Field::XScale)>=1 && e.Ref<float>(0,RuneParticles::Field::XScale)<=1.1f,"Original SystemInit did not replace native initializer");
    std::cout << "RUNE_PARTICLE_PROBE_OK property_backing=1 emitter_motion=1 alpha_scale=1 callback_replace=3 paused=1 system_coords=1 transformed_convergence=1 coords_clear=2 spawn_delay=1 spewer_growth=1 oneshot_cleanup=1 system_lifespan=1 rope_callback=1 beam_geometry=1 rendered=0\n";
}
