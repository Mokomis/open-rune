#pragma once
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Utils/Random.h"
#include "VM/Frame.h"
#include <sstream>
#include <cmath>

// Opt-in measurement only. Original scripts and production world collision,
// controlled time steps in a frozen surrounding world, no actor contacts/saves.
class RuneGibProbe
{
public:
    bool Active() const { return commandline->HasArg("", "--probe-gibs") && !complete; }
    void Tick(Engine& game,int frame)
    {
        if(!Active() || frame<240) return;
        if(!commandline->HasArg("", "--save-directory")) Exception::Throw("Gib probe requires isolated saves");
        auto* player=game.viewport->Actor();
        const vec3 origin=player->Location()+vec3(0,0,96);
        LogMessage("RUNE_GIB_BEGIN controlled_dt=1 surrounding_world_frozen=1 actor_contacts=0 simulation_seconds=8");
        for(const char* cls:{"GibRedMedium","GoblinBHead","goblinlarm","goblinrarm"})
        for(int scenario=0;scenario<2;scenario++) for(int hz:{30,60,120})
        {
            ScopedRandomSeed random(713);
            size_t actorsBefore=game.Level->Actors.size();
            auto* a=player->Spawn(game.packages->GetPackage("RuneI")->GetClass(cls),{},{},origin,Rotator(0,0,0));
            if(!a) Exception::Throw(std::string("Cannot spawn gib fixture ")+cls);
            a->GotoState(a->IsA("Gib")?"FallingGib":"Drop","Begin");
            a->SetBase(nullptr,true);a->SetPhysics(PHYS_Falling);
            a->SetCollision(false,false,false);a->bCollideWorld()=true;
            a->Velocity()=scenario?vec3(100,0,240):vec3(0);a->Acceleration()=vec3(0);
            a->Rotation()={0,0,0};a->DesiredRotation()={16000,24000,8000};a->RotationRate()={12000,18000,0};
            a->LifeSpan()=0;a->InitActorZone();
            if(length(a->Location()-origin)>.01f) Exception::Throw("Gib spawn adjusted origin; not comparable");
            float clock=0,firstHit=-1,settled=-1,peak=a->Location().z,maxStep=0;int hits=0,landed=0;
            vec3 quarter(0),start=a->Location();
            auto previousTraces=Frame::Traces;
            struct Restore {Array<Trace> traces;~Restore(){Frame::Traces=std::move(traces);}} restore{previousTraces};
            Frame::AddTrace("HitWall",[&](UFunction*,UObject* obj,const ArrayView<ExpressionValue>& args){
                if(obj!=a)return;hits++;if(firstHit<0)firstHit=clock;
                auto n=args[0].ToVector();
                if(hits<=12){std::ostringstream s;s<<"RUNE_GIB_HIT class="<<cls<<" toss="<<scenario<<" hz="<<hz<<" time="<<clock<<" n="<<n.x<<","<<n.y<<","<<n.z<<" vz="<<a->Velocity().z<<" z="<<a->Location().z;LogMessage(s.str());}
            });
            Frame::AddTrace("Landed",[&](UFunction*,UObject* obj,const ArrayView<ExpressionValue>&){if(obj==a)landed++;});
            for(int step=0;step<hz*8 && !a->bDeleteMe();step++)
            {
                float dt=1.0f/hz;clock=(step+1)*dt;vec3 before=a->Location();
                a->Tick(dt);a->SetCollision(false,false,false);
                auto pos=a->Location();auto v=a->Velocity();
                if(!std::isfinite(pos.x+pos.y+pos.z+v.x+v.y+v.z)) Exception::Throw("Nonfinite gib motion");
                peak=std::max(peak,pos.z);maxStep=std::max(maxStep,length(pos-before));
                if(step+1==hz/2)quarter=pos-start;
                if(settled<0 && a->Physics()==PHYS_None)settled=clock;
                if(step+1==hz || step+1==hz*2 || step+1==hz*4){std::ostringstream s;s<<"RUNE_GIB_SAMPLE class="<<cls<<" toss="<<scenario<<" hz="<<hz<<" time="<<clock<<" x="<<pos.x-start.x<<" y="<<pos.y-start.y<<" z="<<pos.z-start.z<<" speed="<<length(v)<<" physics="<<(int)a->Physics();LogMessage(s.str());}
            }
            std::ostringstream s;auto end=a->Location()-start;
            s<<"RUNE_GIB_RESULT class="<<cls<<" toss="<<scenario<<" hz="<<hz<<" first_hit="<<firstHit<<" hits="<<hits<<" landed="<<landed<<" settled="<<settled<<" peak="<<peak-start.z<<" half_z="<<quarter.z<<" end_x="<<end.x<<" end_y="<<end.y<<" end_z="<<end.z<<" end_speed="<<length(a->Velocity())<<" physics="<<(int)a->Physics()<<" state="<<a->GetStateName().ToString()<<" max_step="<<maxStep;LogMessage(s.str());
            for(size_t i=actorsBefore;i<game.Level->Actors.size();i++)if(auto* spawned=game.Level->Actors[i];spawned && !spawned->bDeleteMe())spawned->Destroy();
        }
        complete=true;LogMessage("RUNE_GIB_COMPLETE cases=24 physics_changes=0");
    }
private:
    bool complete=false;
};
