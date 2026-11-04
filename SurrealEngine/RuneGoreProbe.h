#pragma once
#include "RuneGore.h"
#include "Packages/Core/UClass.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Collision/TopLevel/CollisionHit.h"

class RuneGoreProbe
{
    struct Pose {UActor* a;vec3 p,v;Rotator r;uint8_t physics;UActor* base;bool carried;};
    std::vector<Pose> poses;
    unsigned checks=0;
    double updateTotal=0;unsigned updateSamples=0;
public:
    void Before(Engine& game,int frame)
    {
        if(!commandline->HasArg("","--probe-gore"))return;
        if(!commandline->HasArg("","--save-directory"))Exception::Throw("Gore fixture requires isolated saves");
        auto* player=game.viewport->Actor();
        if(frame==90) {
            player->Health()=1000;
            vec3 forward=(Coords::Rotation(player->Rotation()).ToMatrix()*vec4(1,0,0,0)).xyz();
            vec3 side(-forward.y,forward.x,0);int i=0;
            for(const char* name:{"GibRedMedium","GoblinBHead","goblinlarm","goblinrarm"}) {
                auto* a=player->Spawn(game.packages->FindClass(std::string("RuneI.")+name),{},{},player->Location()+forward*100.f+side*float(i*12-18)+vec3(0,0,45),{});
                if(!a)Exception::Throw("Gore fixture spawn failed");
                a->GotoState(a->IsA("Gib")?"FallingGib":"Drop","Begin");a->SetBase(nullptr,true);a->SetPhysics(PHYS_Falling);
                a->SetCollision(false,false,false);a->bCollideWorld()=true;
                a->Velocity()=forward*50.f+side*float(i*25-35)+vec3(0,0,190);++i;
            }
            LogMessage("RUNE_GORE_FIXTURE spawned=4 physics=original");
        }
        if(frame==180 || frame==240) {
            UPawn* enemy=nullptr;
            for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA("GoblinWarrior")){enemy=UObject::TryCast<UPawn>(a);if(enemy)break;}
            if(!enemy)Exception::Throw("Gore fixture requires --probe-enemy=GoblinWarrior");
            if(frame==180) {
                CallEvent(enemy,"JointDamaged",{ExpressionValue::IntValue(80),ExpressionValue::ObjectValue(player),ExpressionValue::VectorValue(enemy->Location()),ExpressionValue::VectorValue(vec3(100,0,100)),ExpressionValue::NameValue("sever"),ExpressionValue::IntValue(15)});
                LogMessage("RUNE_GORE_FIXTURE sever_joint=15");
            } else {
                enemy->Health()=0;
                CallEvent(enemy,"Died",{ExpressionValue::ObjectValue(player),ExpressionValue::NameValue("blunt"),ExpressionValue::VectorValue(enemy->Location())});
                LogMessage("RUNE_GORE_FIXTURE death=original");
            }
        }
        if(frame==420)for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA("RuneCarcass")) {
            auto* mesh=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));if(!mesh)continue;
            mesh->PrepareMesh(a->GetByte("SkelMesh"),a->AnimSequence(),a->AnimFrame(),a);
            auto matrix=GetRuneMeshToWorld(a);float lowest=1e20f;
            auto* groups=static_cast<int*>(a->GetProperty("SkelGroupFlags"));
            for(auto& tri:mesh->Tris)if(!(groups[tri.TextureIndex]&PF_Invisible))for(auto i:tri.Indices)
                if(size_t(i)<mesh->Verts.size())lowest=std::min(lowest,(matrix*vec4(mesh->Verts[i],1)).z);
            TraceFlags flags;flags.world=true;auto from=a->Location()+vec3(0,0,20),to=a->Location()-vec3(0,0,100);
            auto h=game.Level->Collision.TraceFirstHit(from,to,nullptr,vec3(0),flags);
            if(h.Actor!=game.LevelInfo || h.Fraction>=1)continue;
            float floor=from.z+(to.z-from.z)*h.Fraction,shift=floor+2.3f-lowest;
            if(std::isfinite(shift) && std::abs(shift)<64 && a->SetLocation(a->Location()+vec3(0,0,shift))) {
                a->SetPhysics(PHYS_None);a->AnimRate()=0;a->bAnimFinished()=true;
                // Force visible coverage of the ordinary presentation path,
                // even if the corpse is behind the close third-person camera.
                RuneGore::MeshTransform(a,mesh,GetRuneMeshToWorld(a));
                LogMessage("RUNE_GORE_CORPSE_FIXTURE visual_gap=2 actor_shift="+std::to_string(shift));
            }
            break;
        }
        poses.clear();
        for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && (a->IsA("Gib")||a->IsA("LimbWeapon")||a->IsA("Carcass")||(a->IsA("Pawn")&&a->GetInt("Health")<=0)))
            poses.push_back({a,a->Location(),a->Velocity(),a->Rotation(),a->Physics(),a->ActorBase(),bool(a->bCarriedItem())});
    }
    void After(int frame)
    {
        if(!commandline->HasArg("","--probe-gore"))return;
        for(auto& p:poses) {
            if(frame>=420 && p.a->IsA("RuneCarcass")) {
                auto* mesh=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p.a));
                if(mesh){mesh->PrepareMesh(p.a->GetByte("SkelMesh"),p.a->AnimSequence(),p.a->AnimFrame(),p.a);RuneGore::MeshTransform(p.a,mesh,GetRuneMeshToWorld(p.a));}
            }
            if(p.a->Location()!=p.p || p.a->Velocity()!=p.v || p.a->Rotation()!=p.r || p.a->Physics()!=p.physics || p.a->ActorBase()!=p.base || bool(p.a->bCarriedItem())!=p.carried)
                Exception::Throw("Gore presentation changed authoritative actor state");
            ++checks;
        }
        if(frame==480)for(auto& p:poses)if(p.a->IsA("Carcass")||p.a->IsA("Pawn"))
            LogMessage("RUNE_GORE_BODY class="+p.a->Class->Name.ToString()+" physics="+std::to_string(p.a->Physics())+" rate="+std::to_string(p.a->AnimRate())+" frame="+std::to_string(p.a->AnimFrame())+" last="+std::to_string(p.a->AnimLast())+" finished="+std::to_string(bool(p.a->bAnimFinished())));
        if(frame>=90){updateTotal+=RuneGore::Stats().updateUs;++updateSamples;}
        if(frame%120==0) {
            auto s=RuneGore::Stats();
            LogMessage("RUNE_GORE_PROFILE frame="+std::to_string(frame)+" bursts="+std::to_string(s.bursts)+" fragments="+std::to_string(s.fragments)+" impacts="+std::to_string(s.impacts)+" stains="+std::to_string(s.stains)+" wounds="+std::to_string(s.wounds)+" settles="+std::to_string(s.settles)+" active="+std::to_string(s.active)+" sleeping="+std::to_string(s.sleeping)+" drawn="+std::to_string(s.drawn)+" traces="+std::to_string(s.traces)+" preserved="+std::to_string(checks));
        }
    }
    void Finish()
    {
        if(!commandline->HasArg("","--probe-gore"))return;
        auto s=RuneGore::Stats();
        if(checks<100)Exception::Throw("Gore fixture did not cover rendered authoritative actors");
        if(RuneGore::Enabled && (s.fragments==0 || s.impacts==0 || s.stains==0))Exception::Throw("Enhanced gore fixture did not exercise fragments and impacts");
        if(!RuneGore::Enabled && (s.fragments || s.stains))Exception::Throw("Original gore emitted enhanced effects");
        LogMessage("RUNE_GORE_UPDATE_US mean="+std::to_string(updateSamples?updateTotal/updateSamples:0));
        LogMessage("RUNE_GORE_OK enhanced="+std::to_string(RuneGore::Enabled.load())+" preserved="+std::to_string(checks));
    }
};
