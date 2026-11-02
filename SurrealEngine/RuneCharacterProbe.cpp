#include "Precomp.h"
#include "Engine.h"
#include "RuneCharacters.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneDualWield.h"
#include "RuneBloodlust.h"
#include "RuneRushPose.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "Utils/CommandLine.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include <set>
#include <iostream>
namespace RuneCharacters {
void Probe(Engine& game) {
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    for(const auto* text:{"DM-Hildir?OpenRuneCharacter=11?OpenRuneSkin=1", "DM-Hildir#entry?OpenRuneCharacter=11?OpenRuneSkin=1"}) {
        UnrealURL parsed(text);check(parsed.GetOption("OpenRuneCharacter")=="11" && parsed.GetOption("OpenRuneSkin")=="1","URL lost final option");
        UnrealURL relative(parsed,UnrealURL("SinkingShip?OpenRuneSkin=0"));
        check(relative.GetOption("OpenRuneCharacter")=="11" && relative.GetOption("OpenRuneSkin")=="0","relative URL did not merge options");
    }
    RuneTactics::Enabled=false;RuneKick::Enabled=true;RuneBash::Enabled=true;RuneBloodlust::Enabled=true;
    int failures=0,count=0;
    const int only=commandline->GetArgInt("","--roster-only",-1);
    for(size_t c=0;c<openrune::Characters.size();++c)for(int skin=0;skin<openrune::Characters[c].skins;++skin) {
        if(only>=0 && int(c)!=only)continue;
        const auto& entry=openrune::Characters[c];
        try {
            RuneDualWield::Enabled=false;Campaign.store(int(c)|(skin<<8));
            auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");
            game.LoadMap(url);game.LoginPlayer();auto* p=game.viewport->Actor();
            check(p && p->Class==game.packages->FindClass(entry.className),"selected class not spawned");
            check(p->GetInt("CurrentSkin")==skin,"skin not applied");
            auto tick=[&](int n){for(int i=0;i<n;++i){float dt=1.f/120;game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);}};
            p->bShowMenu()=false;tick(60);p->DisableEvent("PlayerTick");
            auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));check(model,"missing skeleton");
            auto* geometry=GetRuneGeometry(p);check(geometry && !geometry->Meshes.empty(),"missing character geometry");
            auto* originalSub=p->Class->GetDefaultObject<UActor>()->GetUObject("SubstituteMesh");
            check(p->GetUObject("SubstituteMesh")==originalSub,"replacement body lost");
            if(geometry!=model) {
                check(geometry->Joints.size()==model->Joints.size(),"replacement joint count mismatch");
                for(size_t j=0;j<geometry->Joints.size();++j) check(geometry->Joints[j].Name==model->Joints[j].Name && geometry->Joints[j].Parent==model->Joints[j].Parent,"replacement joint layout mismatch");
            }
            auto meshIndex=p->GetByte("SkelMesh");check(meshIndex<geometry->Meshes.size(),"invalid character mesh variant");
            model->PrepareMesh(meshIndex,p->AnimSequence(),p->AnimFrame(),p);
            check(model->Verts.size()==geometry->Meshes[meshIndex].Vertices.size(),"renderer used wrong character body");
            for(auto v:model->Verts)check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"nonfinite character vertices");
            std::cout<<"ROSTER_GEOMETRY id="<<c<<" geometry="<<geometry->Name.ToString()<<" variant="<<int(meshIndex)<<" vertices="<<model->Verts.size()<<std::endl;
            // Inventory only the texture groups used by the selected body, including
            // runtime skin overrides. Full package identities match the pack loader.
            std::set<int> groups;for(const auto& tri:geometry->Meshes[meshIndex].Triangles)groups.insert(tri.Group);
            auto** skins=static_cast<UObject**>(p->GetProperty("SkelGroupSkins"));
            for(int group:groups) {
                auto* texture=UObject::TryCast<UTexture>(skins[group]);
                if(!texture && !geometry->Meshes[meshIndex].SkinNames[group].IsNone())texture=UObject::TryCast<UTexture>(geometry->package->GetUObject("Texture",geometry->Meshes[meshIndex].SkinNames[group]));
                if(!texture)continue;
                std::string name;auto* package=texture->package;
                for(int index=int(texture->exportIndex)+1;index>0;index=package->GetExportEntry(index)->ObjOuter)
                    name=package->GetName(package->GetExportEntry(index)->ObjName).ToString()+(name.empty()?"":"."+name);
                std::cout<<"ROSTER_TEXTURE id="<<c<<" skin="<<skin<<" group="<<group<<" identity="<<package->GetPackageName().ToString()<<"."<<name<<std::endl;
            }
            check(RuneAnimation::Interpolate,"smooth animation disabled in roster fixture");
            auto error=[](const Array<mat4>& a,const Array<mat4>& b){float e=0;for(size_t j=0;j<a.size();++j)for(int k=0;k<16;++k)e=std::max(e,std::abs(a[j][k]-b[j][k]));return e;};
            int moving=0;size_t sample=0;
            for(size_t sequence=0;sequence<model->Sequences.size();++sequence) {
                auto n=model->Sequences[sequence].Info.NumFrames;if(n<2)continue;
                auto a=model->DecodeFrame(sequence,0),b=model->DecodeFrame(sequence,1),expected=a;
                for(size_t j=0;j<a.size();++j)expected[j]=BlendRunePose(a[j],b[j],.5f);
                auto fractional=model->JointMatrices(model->SampleFrame(sequence,.5f/n,false));
                check(error(fractional,model->JointMatrices(expected))<.005f,"fractional joint interpolation mismatch");
                if(error(fractional,model->JointMatrices(a))>.0001f){++moving;sample=sequence;}
                auto last=model->DecodeFrame(sequence,n-1);auto wrap=last;
                for(size_t j=0;j<a.size();++j)wrap[j]=BlendRunePose(last[j],a[j],.5f);
                check(error(model->JointMatrices(model->SampleFrame(sequence,1-.5f/n,true)),model->JointMatrices(wrap))<.01f,"loop seam interpolation mismatch");
                check(error(model->JointMatrices(model->SampleFrame(sequence,1.1f,false)),model->JointMatrices(last))<.0001f,"nonloop animation wrapped");
            }
            check(moving>0,"no interpolated moving sequences");
            auto sequenceName=model->Sequences[sample].Info.Name;
            p->PlayAnim(sequenceName,1,0);p->AnimFrame()=.271f;
            auto before=model->JointMatrices(model->SampleAnimation(p));
            p->PlayAnim("neutral_idle",1,.2f);
            check(error(before,model->JointMatrices(model->SampleAnimation(p)))<.005f,"character transition snapped");
            p->AnimFrame()*=.5f;auto middle=model->JointMatrices(model->SampleAnimation(p));p->PlayAnim(sequenceName,1,.15f);
            check(error(middle,model->JointMatrices(model->SampleAnimation(p)))<.005f,"interrupted transition snapped");
            p->AnimFrame()=.271f;
            auto bones=model->ActorJointMatrices(p);model->PrepareMesh(meshIndex,p->AnimSequence(),p->AnimFrame(),p);
            const auto& vertices=geometry->Meshes[meshIndex].Vertices;
            for(size_t v=0;v<vertices.size();++v){const auto& vertex=vertices[v];auto expected=(bones[vertex.Joint[0]]*vec4(vertex.Point[0],1)).xyz()*vertex.Weight;
                if(vertex.Weight!=1)expected+=(bones[vertex.Joint[1]]*vec4(vertex.Point[1],1)).xyz()*(1-vertex.Weight);
                check(length(expected-model->Verts[v])<.001f,"body skin and attachment interpolation disagree");}
            p->PlayAnim("neutral_idle",1,0);
            std::cout<<"ROSTER_INTERPOLATION_OK id="<<c<<" skin="<<skin<<" sequences="<<model->Sequences.size()<<" moving="<<moving<<" transitions=1 attachments=1"<<std::endl;
            check(RuneKick::Compatible(p),"kick animation/notify missing");
            check(RuneBash::Compatible(p),"bash animation missing");
            check(RuneDualWield::Eligible(p),"dual wield skeleton incompatible");
            check(RuneRushPose::Supported(p),"shield charge skeleton incompatible");
            auto give=[&](const char* name,bool acquire=true){auto* a=p->Spawn(game.packages->GetPackage("RuneI")->GetClass(name),{},{},p->Location()+vec3(0,0,100),{});check(a,"inventory spawn failed");a->SetFloat("RespawnTime",0);auto* w=CallEvent(a,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();check(w==a,"inventory copy failed");if(acquire)CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(w)});w->GotoState("Idle","Begin");return w;};
            auto reset=[&]{p->Health()=1000;p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);p->GotoState("PlayerWalking","Begin");p->GetUObject("AnimProxy")->GotoState("Idle","Begin");p->SetPhysics(PHYS_Walking);p->Velocity()=p->Acceleration()=vec3(0);p->RuneKickActive=false;p->RuneKickCooldown=0;p->RuneBashPhase=0;p->Bloodlust={};};
            reset();check(RuneKick::Start(p),"kick rejected");tick(360);check(!p->RuneKickActive,"kick never recovered");
            reset();give("VikingShield");check(RuneBash::Start(p),"bash rejected");tick(360);check(!p->RuneBashPhase,"bash never recovered");
            reset();give("VikingShortSword");auto* off=UObject::TryCast<UWeapon>(give("HandAxe",false));RuneDualWield::Enabled=true;
            check(RuneDualWield::Equip(p,off),"offhand equip failed");check(RuneDualWield::Equipped(p),"offhand attachment invalid");
            RuneDualWield::Guard(p,true);tick(30);RuneDualWield::Guard(p,false);
            for(int attack=0;attack<4;++attack){CallEvent(p->GetUObject("AnimProxy"),"Attack");tick(150);}
            check(RuneDualWield::Equipped(p),"offhand lost during attacks");
            RuneDualWield::Unequip(p);RuneDualWield::Enabled=false;
            reset();CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
            p->SetBool("bBloodLust",true);p->SetInt("Strength",p->GetInt("MaxStrength"));
            check(RuneBloodlust::Readiness(p)==3,"bloodlust move readiness");
            check(RuneBloodlust::Charge(p),"shield charge rejected");tick(360);check(!RuneBloodlust::Busy(p),"charge never recovered");
            reset();p->SetBool("bBloodLust",true);p->SetInt("Strength",p->GetInt("MaxStrength"));
            bool leaped=false;for(int direction=0;direction<8 && !leaped;++direction){float angle=direction*3.14159265f/4;leaped=RuneBloodlust::Leap(p,vec3(std::cos(angle),std::sin(angle),0));}
            check(leaped,"no usable bloodlust leap");tick(480);check(!RuneBloodlust::Busy(p),"leap never recovered");
            check(std::isfinite(p->Location().x) && std::isfinite(p->Location().z),"nonfinite movement");
            const auto modelName=model->Name.ToString();const auto jointCount=model->Joints.size();
            if(c==0 || c==11 || c==19 || c==25 || c==26 || c==28 || c==35) {
                reset();p->SetBool("bBloodLust",false);p->SetInt("Strength",0);
                game.SaveGameToSlot(89,"Character roster regression");
                Campaign=-1; // Loading a save must ignore the current menu preference.
                game.LoadFromSaveFile(UnrealURL("?load=89"));game.PossessSavedPlayer();
                p=game.viewport->Actor();
                check(p->Class==game.packages->FindClass(entry.className) && p->GetInt("CurrentSkin")==skin,"save changed character/skin");
                check(game.LevelInfo->URL.GetOption("OpenRuneCharacter")==std::to_string(c),"save lost campaign selection");
                auto next=UnrealURL(game.LevelInfo->URL,UnrealURL("SinkingShip.run"));
                game.LoadMap(next);game.LoginPlayer();p=game.viewport->Actor();
                check(p->Class==game.packages->FindClass(entry.className) && p->GetInt("CurrentSkin")==skin,"story stage replaced selection");
                std::cout<<"ROSTER_SAVE_TRAVEL_OK class="<<entry.className<<" skin="<<skin<<std::endl;
            }
            std::cout<<"ROSTER_OK id="<<c<<" class="<<entry.className<<" skin="<<skin<<" model="<<modelName<<" joints="<<jointCount<<std::endl;++count;
        }catch(const std::exception& e){++failures;std::cout<<"ROSTER_FAIL id="<<c<<" class="<<entry.className<<" skin="<<skin<<" reason="<<e.what()<<std::endl;}
    }
    Campaign=-1;
    std::cout<<"ROSTER_RESULT passed="<<count<<" failed="<<failures<<std::endl;
    if(failures)throw std::runtime_error("character compatibility failures");
}
}
