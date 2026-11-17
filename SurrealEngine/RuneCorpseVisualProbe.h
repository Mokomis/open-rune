#pragma once
#include "Engine.h"
#include "RuneCorpse.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/CommandLine.h"
#include "VM/ScriptCall.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include <fstream>
class RuneCorpseVisualProbe {
    UActor* body=nullptr;unsigned checks=0;
    bool Demo()const{return commandline->HasArg("","--corpse-demo");}
    int PickupFrame()const{return Demo()?600:350;}
    int ThrowFrame()const{return Demo()?1800:510;}
    void Capture(Engine& game,const char* stage) {
        auto prefix=commandline->GetArg("","--corpse-demo-capture","");if(prefix.empty())return;
        auto* device=game.window->GetRenderDevice();int width=device->GetRenderWidth(),height=device->GetRenderHeight();
        Array<TextureColor> pixels;pixels.resize(size_t(width)*height);bool gamma=device->GammaCorrectScreenshots;
        device->GammaCorrectScreenshots=true;device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=gamma;
        std::ofstream out(prefix+"-"+stage+".ppm",std::ios::binary);out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(auto c:pixels){const char rgb[]={char(c.R),char(c.G),char(c.B)};out.write(rgb,3);}
        LogMessage(std::string("RUNE_CORPSE_DEMO_CAPTURE stage=")+stage);
    }
public:
    void Before(Engine& game,int frame) {
        if(!commandline->HasArg("","--probe-corpse"))return;
        if(!commandline->HasArg("","--save-directory"))Exception::Throw("Corpse visual fixture requires isolated saves");
        auto* p=game.viewport->Actor();
        if(frame==90) {
            p->Health()=1000;UPawn* enemy=nullptr;
            for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA(commandline->GetArg("","--probe-enemy","GoblinWarrior"))){enemy=UObject::TryCast<UPawn>(a);break;}
            if(!enemy)Exception::Throw("Corpse visual fixture needs its --probe-enemy actor");
            enemy->Health()=0;CallEvent(enemy,"Died",{ExpressionValue::ObjectValue(p),ExpressionValue::NameValue("blunt"),ExpressionValue::VectorValue(enemy->Location())});
        }
        if(frame==PickupFrame()) {
            for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA("RuneCarcass") && RuneCorpse::Throwable(a)){body=a;break;}
            if(!body) {
                for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA("Carcass")) {
                    LogMessage("CORPSE_VISUAL_CANDIDATE class="+a->Class->Name.ToString()+" hidden="+std::to_string(bool(a->bHidden()))+" decorative="+std::to_string(a->GetBool("bDecorative"))+" mass="+std::to_string(a->Mass())+" radius="+std::to_string(a->CollisionRadius())+" height="+std::to_string(a->CollisionHeight())+" finished="+std::to_string(bool(a->bAnimFinished())));
                    if(auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a)))for(auto& j:m->Joints)LogMessage("CORPSE_JOINT "+j.Name.ToString());
                }
                Exception::Throw("No intact eligible corpse in visual fixture");
            }
            if(commandline->HasArg("","--corpse-pose-audit")) {
                auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(body));auto pose=m->ActorPose(body);
                for(size_t j=0;j<pose.size();++j){auto q=pose[j];LogMessage("CORPSE_POSE_AUDIT joint="+m->Joints[j].Name.ToString()+" parent="+std::to_string(m->Joints[j].Parent)+" position="+std::to_string(q.Position.x)+","+std::to_string(q.Position.y)+","+std::to_string(q.Position.z)+" scale="+std::to_string(q.Scale.x)+","+std::to_string(q.Scale.y)+","+std::to_string(q.Scale.z));}
            }
            if(commandline->HasArg("","--corpse-hand-audit")){
                p->bBehindView()=true;
                for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint")){
                    auto center=a->Location()+vec3(0,0,48);bool clear=true;
                    for(int i=0;i<8;++i){float angle=i*.785398163f;clear=clear&&p->FastTrace(center+vec3(std::cos(angle)*170,std::sin(angle)*170,22),center);}
                    if(clear && p->SetLocation(a->Location()))break;
                }
            }
            p->Velocity()=p->Acceleration()=vec3(0);p->GotoState("PlayerWalking","Begin");p->GetUObject("AnimProxy")->GotoState("Idle","Begin");p->SetPhysics(PHYS_Walking);
            auto forward=vec3(std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0);
            body->SetBase(nullptr,true);body->SetPhysics(PHYS_None);body->Velocity()=vec3(0);body->SetLocation(p->Location()+forward*65.f-vec3(0,0,24));
            if(Demo()) {
                // This fixture tests the real engine input binding, with loose
                // equipment removed from its isolated pickup area beforehand.
                std::vector<UActor*> loot;for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->IsA("Inventory") && !a->Owner() && length(a->Location()-p->Location())<150)loot.push_back(a);
                for(auto* a:loot)a->Destroy();
                game.InputEvent(EInputKey::IK_Ctrl,IST_Press);
            }else if(!RuneCorpse::Pickup(p,body))Exception::Throw("Visual corpse pickup failed");
        }
        if(commandline->HasArg("","--corpse-hand-audit") && frame>=PickupFrame()+10 && frame<ThrowFrame()) {
            p->bBehindView()=true;p->DisableEvent("PlayerTick");p->Velocity()=p->Acceleration()=vec3(0);
            int view=std::clamp((frame-PickupFrame()-10)/30,0,3);float angle=p->Rotation().YawRadians()+view*1.570796327f;
            auto* carryModel=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));auto carryBones=carryModel->ActorJointMatrices(p);auto world=GetRuneMeshToWorld(p);vec3 target(0);int grips=0;
            for(size_t j=0;j<carryModel->Joints.size();++j)if(carryModel->Joints[j].Name=="lwrist"||carryModel->Joints[j].Name=="rwrist"){target+=(world*carryBones[j]*vec4(0,0,0,1)).xyz();++grips;}target/=float(std::max(grips,1));
            game.CameraLocation=target+vec3(std::cos(angle)*150,std::sin(angle)*150,0);game.CameraRotation=Rotator::FromVector(target-game.CameraLocation);game.CameraFovAngle=60;
            if(frame==PickupFrame()+10){
                auto* mesh=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));auto* geom=GetRuneGeometry(p);int variant=p->GetByte("SkelMesh");if(variant>=int(geom->Meshes.size()))variant=0;
                const auto& verts=geom->Meshes[variant].Vertices;auto bones=mesh->ActorJointMatrices(p);
                for(size_t j=0;j<mesh->Joints.size();++j){int count=0;vec3 low(1e10f),high(-1e10f);for(const auto& v:verts)for(int k=0;k<2;++k)if(v.Joint[k]==int(j)&&(k==0?v.Weight>0:v.Weight<1)){++count;for(int axis=0;axis<3;++axis){low[axis]=std::min(low[axis],v.Point[k][axis]);high[axis]=std::max(high[axis],v.Point[k][axis]);}}
                    LogMessage("CARRY_HAND_RIG joint="+mesh->Joints[j].Name.ToString()+" parent="+std::to_string(mesh->Joints[j].Parent)+" vertices="+std::to_string(count)+" min="+std::to_string(low.x)+","+std::to_string(low.y)+","+std::to_string(low.z)+" max="+std::to_string(high.x)+","+std::to_string(high.y)+","+std::to_string(high.z));
                }
            }
        }
        if(Demo()) {
            if(frame==PickupFrame()+1)game.InputEvent(EInputKey::IK_Ctrl,IST_Release);
            if(frame==ThrowFrame())game.InputEvent(EInputKey::IK_LeftMouse,IST_Press);
            if(frame==ThrowFrame()+1)game.InputEvent(EInputKey::IK_LeftMouse,IST_Release);
            if(frame%120==0) {
                std::string phase=frame<PickupFrame()?"Preparing body":frame<ThrowFrame()?"Holding corpse overhead":"Throw completed";
                game.window->SetWindowTitle("Open Rune — Corpse demo: "+phase);
                CallEvent(p,"ClientMessage",{ExpressionValue::StringValue("AUTOMATED CORPSE TEST: "+phase)});
            }
        }else if(frame==ThrowFrame())CallEvent(p,"Throw");
        if(commandline->HasArg("","--corpse-motion-demo")) {
            if(frame==PickupFrame()+20)game.InputEvent(EInputKey::IK_W,IST_Press);
            if(frame>=PickupFrame()+50 && frame<PickupFrame()+95){p->ViewRotation().Yaw+=160;p->Rotation().Yaw=p->ViewRotation().Yaw;}
            if(frame==PickupFrame()+100)game.InputEvent(EInputKey::IK_W,IST_Release);
        }
    }
    void After(Engine& game,int frame) {
        if(!commandline->HasArg("","--probe-corpse") || frame<PickupFrame()+1)return;
        auto* p=game.viewport->Actor();
        if(frame<ThrowFrame() && RuneCorpse::Held(p)!=body)Exception::Throw("Visual carry lost body");
        auto* mesh=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));auto bones=mesh->ActorJointMatrices(p);
        for(auto& m:bones)for(int i=0;i<16;++i)if(!std::isfinite(m[i]))Exception::Throw("Nonfinite carry pose");
        if(commandline->HasArg("","--corpse-pose-audit") && frame==PickupFrame()+120) {
            auto transform=GetRuneMeshToWorld(body);std::string scales;
            for(int c=0;c<3;++c)scales+=std::to_string(length(vec3(transform[c*4],transform[c*4+1],transform[c*4+2])))+",";
            LogMessage("CORPSE_POSE_AUDIT world_scales="+scales+" draw_scale="+std::to_string(body->DrawScale()));
        }
        if(commandline->HasArg("","--corpse-hand-audit") && frame>=PickupFrame()+30 && frame<=PickupFrame()+120 && (frame-PickupFrame())%30==0)Capture(game,("hands-"+std::to_string((frame-PickupFrame())/30)).c_str());
        if(Demo() && frame==PickupFrame()+120)Capture(game,"carrying");
        if(Demo() && frame==ThrowFrame()+15)Capture(game,"throw-windup");
        if(Demo() && frame==ThrowFrame()+45)Capture(game,"released");
        if(commandline->HasArg("","--corpse-motion-demo") && frame>=PickupFrame()+40 && frame<=PickupFrame()+140 && frame%20==10)
            Capture(game,("motion-"+std::to_string(frame-PickupFrame())).c_str());
        ++checks;
    }
    void Finish() {if(checks)LogMessage("RUNE_CORPSE_RENDER_OK checks="+std::to_string(checks));}
};
