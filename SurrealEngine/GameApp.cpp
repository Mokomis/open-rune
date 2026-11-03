
#include "Precomp.h"
#include "Render/VisibleSnow.h"
#include "Render/VisibleRuneEffects.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "RuneDynamicProbe.h"
#include "RunePolyobjProbe.h"
#include "RuneLightingVisibilityProbe.h"
#include "RuneCampaignNativeProbe.h"
#include "RuneCameraProbe.h"
#include "RunePolyobjNativeProbe.h"
#include "RuneAudioProbe.h"
#include "RuneNavigationCanvasProbe.h"
#include "RuneActorReachProbe.h"
#include "RuneCollisionProbe.h"
#include "RuneParticleProbe.h"
#include "RuneCoreVectorProbe.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "UI/Launcher/RuneSetupWindow.h"
#include "Utils/File.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Package/IniFile.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "VM/ExpressionValue.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Native/NObject.h"
#include "Native/NRune.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <iomanip>
#include "RuneEnemyGeometryProbe.h"
#include "RuneIniProbe.h"
#include "RuneAnimationProbe.h"
#include "RuneKick.h"
#include "RuneBash.h"
void RunRuneTacticsProbe(Engine& game);
#include "RuneTextureExport.h"
#include "RuneSoundExport.h"
#include "RuneSoundPackProbe.h"
#include "Render/ShadowSettings.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#include "RuneMultiplayer.h"
#endif

int GameApp::main(Array<std::string> args)
{
	// A package probe must also run on an Android shell without an Activity.
	if (std::find(args.begin(),args.end(),"--rune-net-bot")==args.end() && std::none_of(args.begin(),args.end(),[](const std::string& a){return a.starts_with("--rune-tactics-probe");}) && std::find(args.begin(),args.end(),"--rune-probe")==args.end() && std::find(args.begin(),args.end(),"--rune-network-probe")==args.end() && std::none_of(args.begin(),args.end(),[](const std::string& a){return a.starts_with("--rune-network-process-probe") || a.starts_with("--rune-server");}) && std::find(args.begin(),args.end(),"--rune-export-textures")==args.end() && std::find(args.begin(),args.end(),"--rune-export-sounds")==args.end() && std::find(args.begin(),args.end(),"--sound-pack-probe")==args.end()) InitWidgetResources("dark");

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;
        RuneAnimation::Interpolate=commandline->GetArgInt("", "--rune-animation-interpolation", 1)!=0;
        RuneShadows::RequestedBudget=commandline->GetArgInt("","--shadow-lights",0);

        if(commandline->HasArg("","--sound-pack-probe")){
            if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Sound-pack probe requires isolated saves");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1||!GameFolderSelection::Games[0].IsRune())throw std::runtime_error("Pass one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0),true);
            ProbeRuneSoundPack(game,commandline->GetArg("","--sound-probe-package",""),commandline->GetArg("","--sound-probe-output",""));return 0;
        }

        if(commandline->HasArg("", "--rune-export-sounds")) {
            if(!commandline->HasArg("", "--save-directory"))throw std::runtime_error("Offline sound export requires isolated --save-directory");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1||!GameFolderSelection::Games[0].IsRune())throw std::runtime_error("Pass exactly one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0),true);
            ExportRuneSounds(game,commandline->GetArg("","--sound-package",""),commandline->GetArg("","--sound-output",""));
            return 0;
        }

        if (commandline->HasArg("", "--rune-export-textures"))
        {
            GameFolderSelection::UpdateList();
            if (GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune())
                throw std::runtime_error("Pass exactly one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0), true);
            ExportRuneTextures(game, commandline->GetArg("", "--texture-package", ""), commandline->GetArg("", "--texture-output", ""));
            return 0;
        }

#ifdef OPENRUNE_ENABLE_NETWORK
        if(commandline->HasArg("","--rune-server") || commandline->HasArg("","--rune-join"))
        {
            Logger::Get()->SetCallback([](const LogMessageLine& line){std::cerr<<line.Text<<'\n';});
            bool server=commandline->HasArg("","--rune-server");
            if(server && commandline->HasArg("","--rune-join"))throw std::runtime_error("Choose hosting or joining");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune())throw std::runtime_error("Pass one Rune installation");
            int seconds=commandline->GetArgInt("","--rune-net-seconds",0);
            if(seconds<0 || seconds>86400)throw std::runtime_error("Invalid multiplayer duration");
            Engine game(GameFolderSelection::GetLaunchInfo(0));
            if(server)RunRuneMultiplayerServer(game,commandline->GetArg("","--rune-server",""));
            else if(commandline->HasArg("","--rune-net-bot"))RunRuneMultiplayerBot(game,commandline->GetArg("","--rune-join",""));
            else RunRuneMultiplayerClient(game,commandline->GetArg("","--rune-join",""));
            return 0;
        }
        if (commandline->HasArg("", "--rune-network-process-probe"))
        {
            Logger::Get()->SetCallback([](const LogMessageLine& line){std::cerr<<line.Text<<'\n';});
            if (!commandline->HasArg("", "--save-directory")) throw std::runtime_error("Network process fixture requires isolated saves");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune()) throw std::runtime_error("Pass one Rune installation");
            auto role=commandline->GetArg("", "--rune-network-process-probe", "");
            auto port=commandline->GetArgInt("", "--rune-network-process-port", 0);
            auto latency=commandline->GetArgInt("", "--rune-network-process-latency", 0);
            auto loss=commandline->GetArgInt("", "--rune-network-process-loss", 0);
            if((role!="server" && role!="client" && role!="server-natural" && role!="client-natural" &&
                role!="server-dual-natural" && role!="client-two-natural") ||
                port<0 || port>65535 || (role.starts_with("client") && !port) ||
                latency<0 || latency>250 || loss<0 || loss>20)
                throw std::runtime_error("Network process fixture needs server or client and client port");
            Engine game(GameFolderSelection::GetLaunchInfo(0));
            RunRuneNetworkProcessProbe(game,role,uint16_t(port),unsigned(latency),unsigned(loss)); return 0;
        }
        if (commandline->HasArg("", "--rune-network-probe"))
        {
            Logger::Get()->SetCallback([](const LogMessageLine& line){std::cerr<<line.Text<<'\n';});
            if (!commandline->HasArg("", "--save-directory")) throw std::runtime_error("Network fixture requires isolated saves");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune()) throw std::runtime_error("Pass one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0));
            if(commandline->HasArg("","--rune-net-native-combat"))RunRuneNetworkNativeProbe(game);else RunRuneNetworkProbe(game); return 0;
        }
#endif
        if(commandline->HasArg("","--snow-probe")) {
            Logger::Get()->SetCallback([](const LogMessageLine& line){std::cerr<<line.Text<<'\n';});
            if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Snow probe requires isolated saves");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune())throw std::runtime_error("Pass one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0));game.NetworkAuthority=true;
            auto map=commandline->GetArg("","--snow-probe","RagnarVillage");auto url=game.GetDefaultURL(map+".run");game.LoadMap(url);RuneSnow::Reset();
            unsigned open=0,closed=0,authored=0,protectedCount=0,leaks=0;
            for(auto* actor:game.Level->Actors)if(actor && actor->IsA("Snow"))++authored;
            for(auto* actor:game.Level->Actors)if(actor && actor->IsA("NavigationPoint")) {
                auto p=actor->Location()+vec3(0,0,40);
                auto col=RuneSnow::Sample(int(std::floor(p.x/64)),int(std::floor(p.y/64)),int(std::floor(p.z/64)),0);
                bool exposed=col.open && p.z>col.bottom && p.z<col.top;
                if(exposed){++open;if(!RuneSnow::Sky(RuneSnow::Cast(p,p+vec3(0,0,32768))))++leaks;}else ++closed;
                if(col.open && p.z<=col.bottom)++protectedCount;
                if(exposed && open<=12)std::cout<<"SNOW_VIEW x="<<p.x<<" y="<<p.y<<" z="<<p.z<<" floor="<<col.bottom<<" sky="<<col.top<<std::endl;
            }
            std::cout<<"SNOW_GEOMETRY map="<<map<<" sky="<<RuneSnow::weather.sky<<" open="<<open<<" closed="<<closed<<" roofProtected="<<protectedCount<<" authoredSnow="<<authored<<" leaks="<<leaks<<std::endl;
            if(leaks)throw std::runtime_error("Snow probe exposed a covered navigation point");
            return 0;
        }
        if(commandline->HasArg("", "--rune-tactics-probe")) {
            Logger::Get()->SetCallback([](const LogMessageLine& line){std::cerr<<line.Text<<'\n';});
            if(!commandline->HasArg("", "--save-directory"))throw std::runtime_error("Tactics fixture requires isolated saves");
            GameFolderSelection::UpdateList();
            if(GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune())throw std::runtime_error("Pass one Rune installation");
            Engine game(GameFolderSelection::GetLaunchInfo(0));RunRuneTacticsProbe(game);return 0;
        }
        // Headless package validation: no game loop, config saves, or actor events.
		if (commandline->HasArg("", "--rune-probe"))
		{
			Logger::Get()->SetCallback([](const LogMessageLine& line) { std::cerr << line.Text << '\n'; });
			try
			{
				GameFolderSelection::UpdateList();
				if (GameFolderSelection::Games.size()!=1 || !GameFolderSelection::Games[0].IsRune())
					throw std::runtime_error("Pass exactly one recognized Rune installation folder");
				Engine probe(GameFolderSelection::GetLaunchInfo(0), true);
				IniFile originalUser(probe.LaunchInfo.gameRootFolder+"/System/User.ini");
				if (originalUser.GetValue("Engine.Input","W")!="MoveForward" || originalUser.GetValue("DefaultPlayer","Name")!="Ragnar")
					throw std::runtime_error("Rune UTF-16 configuration did not decode");
				std::cout << "RUNE_UNICODE_CONFIG_OK\n";
                if(commandline->HasArg("","--probe-effects-art")) {
                    for(int phase=0;phase<RuneEffects::FlameFrames;phase++) {
                        auto texture=RuneEffects::Texture(phase,true,true);auto& mip=texture.Mips[0];
                        for(int y=107;y<128;y++)for(int x=0;x<128;x++)for(int c=0;c<3;c++)
                            if(mip.Data[(y*128+x)*4+c])throw std::runtime_error("Flame repeated into authored wrap border");
                    }
                    if(RuneEffects::FlameFrames/RuneEffects::FlameFps!=2.f)throw std::runtime_error("Flame animation cycle must be two seconds");
                    std::cout<<"RUNE_FIRE_ART_OK frames=64 cycle_seconds=2 black_wrap_rows=107..127\n";
                }

				std::string iniProbePath = commandline->GetArg("", "--probe-ini-file", "");
				if (!iniProbePath.empty()) RunRuneIniProbe(iniProbePath);
				vec4 cube[6]={{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}};
				float hitTime;vec3 hitNormal;
				if(!NRune::ClipJointBox(cube,{-3,0,0},{1,0,0},0,hitTime,hitNormal) || std::abs(hitTime-0.475f)>0.0001f || hitNormal!=vec3(-1,0,0))
					throw std::runtime_error("Joint box crossing or native hit backoff failed");
				if(NRune::ClipJointBox(cube,{-3,2,0},{3,2,0},0,hitTime,hitNormal)) throw std::runtime_error("Joint box missed ray reported a hit");
				if(!NRune::ClipJointBox(cube,{-3,1.2f,0},{3,1.2f,0},0.25f,hitTime,hitNormal)) throw std::runtime_error("Joint box sweep extent ignored");
				if(!NRune::ClipJointBox(cube,{0,0,0},{3,0,0},0,hitTime,hitNormal) || hitTime!=0) throw std::runtime_error("Joint box start-inside case failed");
				std::cout << "RUNE_JOINT_COLLISION_GEOMETRY_OK\n";
				for (vec3 destination : {vec3(200,0,0),vec3(120,-160,30),vec3(-100,50,-40)})
				{
					vec3 velocity=NRune::ArcVelocity(8000,vec3(0),destination,950.0f);
					float time=length(vec3(destination.x,destination.y,0))/length(vec3(velocity.x,velocity.y,0));
					vec3 landed=velocity*time+vec3(0,0,-475.0f*time*time);
					if (!std::isfinite(time) || length(landed-destination)>0.01f) throw std::runtime_error("Arc velocity misses ballistic destination");
				}
				if (NRune::ArcVelocity(8000,vec3(0),vec3(0),950)!=vec3(0) ||
					NRune::ArcVelocity(8000,vec3(0),vec3(10,0,100),950)!=vec3(0) ||
					NRune::ArcVelocity(16384,vec3(0),vec3(100,0,0),950)!=vec3(0))
					throw std::runtime_error("Invalid ballistic arc must return zero");
				std::cout << "RUNE_BALLISTIC_ARC_OK\n";
				// Regression: structure temporaries must retain the storage allocated by Init,
				// including when named return value optimization elides their move constructor.
				auto* actorClass=probe.packages->GetPackage("Engine")->GetClass("Actor");
				UStructProperty* region=nullptr;
				for (auto* p:actorClass->Properties) if (p->Name=="Region") region=UObject::Cast<UStructProperty>(p);
				if (!region) throw std::runtime_error("PointRegion property missing");
				auto* layout=GC::Alloc<UStruct>(NameString("RunePropertyLookupProbe"),nullptr,ObjectFlags::NoFlags);
				if(layout->FindMemberProperty("Region")) throw std::runtime_error("Empty layout returned a property");
				layout->Properties.push_back(region);
				if(layout->FindMemberProperty("rEgIoN")!=region) throw std::runtime_error("Property cache miss invalidation or case folding failed");
				layout->Properties=actorClass->Properties;
				if(layout->FindMemberProperty("Region")!=region || layout->FindMemberProperty("RuneAbsentProbeField")) throw std::runtime_error("Property cache layout replacement failed");
				std::cout << "RUNE_PROPERTY_CACHE_OK\n";
				auto defaultRegion=ExpressionValue::DefaultValue(region);
				auto copiedRegion=ExpressionValue::PropertyValue(region);
				copiedRegion.Store(defaultRegion);
				bool checked=false;
				for (auto* p:region->Struct->Properties) if (p->ValueType==ExpressionValueType::ValueInt)
				{
					if (copiedRegion.Member(p).ToInt()!=0) throw std::runtime_error("Struct default not zero initialized");
					copiedRegion.Member(p).Store(ExpressionValue::IntValue(37));
					if (defaultRegion.Member(p).ToInt()!=0 || copiedRegion.Member(p).ToInt()!=37) throw std::runtime_error("Struct storage aliases");
					checked=true;
				}
				if (!checked) throw std::runtime_error("PointRegion integer field missing");
				std::cout << "RUNE_VM_STRUCT_OK\n";
				UObject* background=nullptr;
				NObject::DynamicLoadObject("UWindow.Icons.RCBack0",probe.packages->GetPackage("Engine")->GetClass("Texture"),false,background);
				if (!background || background->Name!="RCBack0") throw std::runtime_error("Grouped texture lookup failed");
				std::cout << "RUNE_GROUPED_TEXTURE_OK\n";
				if (commandline->HasArg("", "--probe-enemy-geometry")) RunRuneEnemyGeometryProbe(probe);
				auto* model=UObject::Cast<USkelModel>(probe.packages->GetPackage("Players")->GetUObject("SkelModel", "Ragnar"));
				if (!model) throw std::runtime_error("Ragnar model not found");
				model->ValidateAnimations();
				auto* actor=probe.packages->GetTransientPackage()->NewObject("RuneProbeRagnar",probe.packages->GetPackage("RuneI")->GetClass("Ragnar"),ObjectFlags::Transient);
				actor->SetObject("Skeletal",model);
				auto* animProxy=UObject::Cast<UActor>(probe.packages->GetTransientPackage()->NewObject("RuneProbeProxy",probe.packages->GetPackage("RuneI")->GetClass("RunePlayerProxy"),ObjectFlags::Transient));
				animProxy->SetObject("Owner",actor);
				animProxy->PlayAnim("walkforward7",1.0f,0.0f);
				if (animProxy->AnimSequence()!="walkforward7" || animProxy->AnimRate()<=0 || GetRuneDisplayMesh(animProxy)!=model)
					throw std::runtime_error("Rune animation proxy did not resolve owner skeleton");
				std::cout << "RUNE_ANIMATION_PROXY_OK\n";
				auto* runeActor=UObject::Cast<UActor>(actor);
				auto bodyPose=model->DecodeFrame(0,0,false,runeActor);
				animProxy->AnimFrame()=0.5f;
				actor->SetObject("AnimProxy",animProxy);
				auto layeredPose=model->DecodeFrame(0,0,false,runeActor);
				bool upperChanged=false;
				for (size_t j=0;j<model->Joints.size();j++)
				{
					bool changed=bodyPose[j].Position!=layeredPose[j].Position || bodyPose[j].Rotation!=layeredPose[j].Rotation || bodyPose[j].Scale!=layeredPose[j].Scale;
					if (model->Joints[j].Group==0 && changed) throw std::runtime_error("Upper-body proxy changed a lower-body joint");
					if (model->Joints[j].Group==1 && changed) upperChanged=true;
				}
				if (!upperChanged) throw std::runtime_error("Upper-body proxy did not change the pose");
				actor->SetObject("AnimProxy",nullptr);
				std::cout << "RUNE_LAYERED_POSE_OK\n";
				auto invoke=[&](NameString name, Array<ExpressionValue> args=Array<ExpressionValue>())
				{
					auto* func=FindEventFunction(actor,name);
					if (!func) throw std::runtime_error("Probe native function missing");
					return Frame::Call(func,actor,{args.data(),args.size()});
				};
				if (invoke("NumJoints").ToInt()!=38) throw std::runtime_error("NumJoints VM binding failed");
				invoke("ModifyIntStat",{ExpressionValue::ByteValue(38),ExpressionValue::IntValue(2)});
				invoke("ModifyIntStat",{ExpressionValue::ByteValue(38),ExpressionValue::IntValue(-1)});
				invoke("ModifyFloatStat",{ExpressionValue::ByteValue(41),ExpressionValue::FloatValue(1.25f)});
				invoke("UnlockAchievement",{ExpressionValue::ByteValue(0)});
				if (invoke("GetIntStat",{ExpressionValue::ByteValue(38)}).ToInt()!=1 ||
					invoke("GetIntStat",{ExpressionValue::ByteValue(255)}).ToInt()!=0 ||
					invoke("GetFloatStat",{ExpressionValue::ByteValue(41)}).ToFloat()!=1.25f ||
					!invoke("GetAchievement",{ExpressionValue::ByteValue(0)}).ToBool() ||
					invoke("GetAchievement",{ExpressionValue::ByteValue(255)}).ToBool())
					throw std::runtime_error("Offline Rune statistics VM callbacks failed");
				std::cout << "RUNE_OFFLINE_SESSION_STATS_OK\n";
				int head=invoke("JointNamed",{ExpressionValue::NameValue("Head")}).ToInt();
				if (head<0 || invoke("GetJointName",{ExpressionValue::IntValue(head)}).ToString()!="Head") throw std::runtime_error("Joint name VM bindings failed");
				invoke("SetDefaultPolygroups");
				invoke("SetDefaultJointFlags");
				invoke("ResetAnimationCache",{ExpressionValue::NameValue("neutral_idle")});
				if (UObject::Cast<UActor>(actor)->RuneResetSequence!="neutral_idle") throw std::runtime_error("Reset pose binding failed");
				auto* child=probe.packages->GetTransientPackage()->NewObject("RuneProbeAttachment",actorClass,ObjectFlags::Transient);
				auto* childActor=UObject::Cast<UActor>(child);
				childActor->SetBase(runeActor,false);
				invoke("AttachActorToJoint",{ExpressionValue::ObjectValue(child),ExpressionValue::IntValue(head)});
				if(childActor->ActorBase() || !runeActor->BasedActors.empty()) throw std::runtime_error("Joint attach retained an old physics base");
				if (invoke("ActorAttachedTo",{ExpressionValue::IntValue(head)}).ToObject()!=child || child->GetUObject("AttachParent")!=actor || !child->GetBool("bCarriedItem")) throw std::runtime_error("Attachment bindings failed");
				vec3 pos=invoke("GetJointPos",{ExpressionValue::IntValue(head)}).ToVector();
				for (int joint : {0, head})
				{
					Rotator rotation=invoke("GetJointRot",{ExpressionValue::IntValue(joint)}).ToRotator();
					mat4 actual=Coords::Rotation(rotation).ToMatrix();
					mat4 expected=GetRuneMeshToWorld(runeActor)*model->ActorJointMatrices(runeActor)[joint]*Coords::InverseRotation(model->RotationOffset).ToMatrix();
					for (vec4 axis : {vec4(1,0,0,0),vec4(0,1,0,0),vec4(0,0,1,0)})
						if(dot(normalize((actual*axis).xyz()),normalize((expected*axis).xyz()))<0.999f)
							throw std::runtime_error("GetJointRot orientation does not match joint axes");
				}
				if(invoke("GetJointRot",{ExpressionValue::IntValue(-1)}).ToRotator()!=Rotator(0,0,0) ||
					invoke("GetJointRot",{ExpressionValue::IntValue(50)}).ToRotator()!=Rotator(0,0,0))
					throw std::runtime_error("GetJointRot invalid joint must return zero");
				std::cout << "RUNE_JOINT_ROTATION_OK\n";
				if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) throw std::runtime_error("Joint position binding failed");
				vec3 attached=(GetRuneActorToWorld(UObject::Cast<UActor>(child))*vec4(0,0,0,1)).xyz();
				if(length(attached-pos)>0.001f) throw std::runtime_error("Attachment transform differs from joint position");
				vec3 oldLocation=runeActor->Location();
				runeActor->Location()+=vec3(10,-20,30);
				vec3 moved=(GetRuneActorToWorld(UObject::Cast<UActor>(child))*vec4(0,0,0,1)).xyz();
				if(length((moved-attached)-vec3(10,-20,30))>0.001f) throw std::runtime_error("Attachment did not follow parent translation");
				runeActor->Location()=oldLocation;
				mat4 attachedWorld=GetRuneActorToWorld(UObject::Cast<UActor>(child));
				childActor->SetBase(runeActor,false); // Emulate a legacy saved attachment.
				if(invoke("DetachActorFromJoint",{ExpressionValue::IntValue(head)}).ToObject()!=child || child->GetBool("bCarriedItem") || child->GetUObject("AttachParent") || invoke("ActorAttachedTo",{ExpressionValue::IntValue(head)}).ToObject())
					throw std::runtime_error("Detach did not clear attachment metadata");
				if(childActor->ActorBase() || !runeActor->BasedActors.empty()) throw std::runtime_error("Joint detach retained a legacy physics base");
				mat4 detachedWorld=GetRuneActorToWorld(UObject::Cast<UActor>(child));
				if(length((detachedWorld*vec4(0,0,0,1)).xyz()-attached)>0.001f) throw std::runtime_error("Detached actor jumped from its joint position");
				for(vec4 axis:{vec4(1,0,0,0),vec4(0,1,0,0),vec4(0,0,1,0)})
					if(dot(normalize((attachedWorld*axis).xyz()),normalize((detachedWorld*axis).xyz()))<0.999f) throw std::runtime_error("Detached actor orientation changed");
				if(invoke("DetachActorFromJoint",{ExpressionValue::IntValue(head)}).ToObject()) throw std::runtime_error("Empty joint detach returned an actor");
				std::cout << "RUNE_ATTACHMENT_TRANSFORM_OK\n";
				auto* pawn=UObject::Cast<UActor>(actor);
				invoke("SetJointRot",{ExpressionValue::IntValue(head),ExpressionValue::RotatorValue({101,202,303})});
				if (pawn->RuneJointOffsets[head]!=Rotator(-202,-303,-101)) throw std::runtime_error("Joint rotation swizzle failed");
				invoke("TurnJointTo",{ExpressionValue::IntValue(head),ExpressionValue::RotatorValue(pawn->Rotation()+Rotator(400,500,600))});
				if (pawn->RuneJointOffsets[head]!=Rotator(500,600,400)) throw std::runtime_error("Joint target rotation swizzle failed");
				actor->SetBool("bCanLook",true); actor->SetInt("Health",100); actor->SetBool("bOverrideLookTarget",true);
				actor->SetBool("bRotateHead",true); actor->SetBool("bRotateTorso",false);
				actor->SetRotator("LookAngle",{0,0,0}); actor->SetRotator("targetangle",{0,8192,0});
				actor->SetRotator("MaxHeadAngle",{4096,4096,0}); actor->SetRotator("MaxBodyAngle",{4096,4096,0});
				actor->SetFloat("LookDegPerSec",90); actor->SetFloat("PeripheralVision",-1);
				invoke("SkeletonLook",{ExpressionValue::FloatValue(0.1f)});
				if (actor->GetRotator("LookAngle").Yaw!=1638 || pawn->RuneJointOffsets[head].Pitch!=1638) throw std::runtime_error("Skeleton look rate limit failed");
				actor->SetBool("bCanLook",false);
				invoke("SkeletonLook",{ExpressionValue::FloatValue(1.0f)});
				if (actor->GetRotator("LookAngle").Yaw!=1638) throw std::runtime_error("Disabled look guard failed");
				auto original=model->SkinFrame(0,0,0); auto adjusted=model->SkinFrame(0,0,0,pawn);
				float change=0;
				for(size_t i=0;i<original.size();i++) change+=length(original[i]-adjusted[i]);
				if (!std::isfinite(change) || change<=0.01f) throw std::runtime_error("Joint offsets do not affect mesh skinning");
				std::cout << "RUNE_LOOK_AND_POSE_OK\n";
				auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
				for (size_t i=0;i<model->Joints.size();i++) if (flags[i]!=(uint8_t)model->Joints[i].Flags) throw std::runtime_error("Joint flag VM binding failed");
				std::cout << "RUNE_NATIVE_BINDINGS_OK\n";
				RunRuneDynamicProbe(probe);
                RunRuneAnimationProbe(probe);
                for(const char* cls:{"Ragnar","DarkViking","Conrack","Sark","GoblinWarrior","SnowBeast"}) {
                    auto* p=UObject::Cast<UPawn>(probe.packages->GetTransientPackage()->NewObject(std::string("KickModel")+cls,probe.packages->GetPackage("RuneI")->GetClass(cls),ObjectFlags::Transient));
                    std::cout<<"RUNE_KICK_MODEL class="<<cls<<" compatible="<<RuneKick::Compatible(p)<<'\n';
                    std::cout<<"RUNE_BASH_MODEL class="<<cls<<" compatible="<<RuneBash::Compatible(p)<<'\n';
                    if(RuneBash::Compatible(p)) {
                        auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));
                        p->Rotation()=Rotator(0,0,0);p->PlayAnim("H3_DefendAttack",1,0);
                        for(size_t j=0;j<model->Joints.size();j++)if(model->Joints[j].Name=="attach_shielda") {
                            for(int f=0;f<8;f++) { p->AnimFrame()=f/8.0f;
                                auto position=(GetRuneMeshToWorld(p)*model->ActorJointMatrices(p)[j]*vec4(0,0,0,1)).xyz();
                                std::cout<<"RUNE_BASH_SHIELD class="<<cls<<" frame="<<f<<" x="<<position.x<<" y="<<position.y<<" z="<<position.z<<'\n';
                            }
                        }
                    }
                }
				RunRuneCampaignNativeProbe(probe);
				RunRuneCoreVectorProbe(probe);
				RunRuneCameraProbe(probe);
				RunRunePolyobjNativeProbe(probe);
				RuneAudioProbe::Run(probe);
				RunRuneNavigationCanvasProbe(probe);
				RunRuneActorReachProbe(probe);
				RunRuneCollisionProbe(probe);
				std::string objPath=commandline->GetArg("", "--probe-obj", "");
				if (!objPath.empty())
				{
					std::ofstream file(objPath);
					if (!file) throw std::runtime_error("Cannot create probe OBJ");
					file << std::setprecision(9);
					for (const auto& v:model->SkinFrame(0,0,0)) file << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
					for (const auto& t:model->Meshes[0].Triangles) file << "f " << t.Index[0]+1 << ' ' << t.Index[1]+1 << ' ' << t.Index[2]+1 << '\n';
				}
				model->PrepareMesh(0,"walkforward7",0.25f);
				std::cout << "RUNE_MODEL_OK meshes=" << model->Meshes.size() << " joints=" << model->Joints.size()
					<< " sequences=" << model->Sequences.size() << " frames=" << model->Frames.size()
					<< " vertices=" << model->Verts.size() << " triangles=" << model->Tris.size() << '\n';
				std::string map=commandline->GetArg("", "--probe-map", "");
				if (!map.empty())
				{
					auto* level=UObject::Cast<ULevel>(probe.packages->LoadMap(map)->GetUObject("Level", "MyLevel"));
					if (!level) throw std::runtime_error("Level object not found");
					std::cout << "RUNE_LEVEL_OK map=" << map << " actors=" << level->Actors.size() << '\n';
					if(commandline->HasArg("", "--probe-polyobj")) RunRunePolyobjProbe(probe,level);
                    if(commandline->HasArg("","--probe-lighting-bvh"))RunRuneLightingVisibilityProbe(probe,level);
					if(commandline->HasArg("", "--probe-particles")) RunRuneParticleProbe(probe,level);
					if(commandline->HasArg("", "--probe-village-collision")) RunRuneVillageCollisionProbe(probe,level);
                    if(commandline->HasArg("", "--probe-water-surfaces")) for(auto& surface:level->Model->Surfaces) {
                        if(!surface.Material)continue;
                        std::string name=surface.Material->package->GetPackageName().ToString()+"."+surface.Material->Name.ToString();
                        std::string lower=name;for(char& c:lower)c=(char)std::tolower((unsigned char)c);
                        if(lower.find("water")==std::string::npos && !surface.Material->IsA("WaterTexture"))continue;
                        auto pos=level->Model->Points[surface.pBase],normal=level->Model->Vectors[surface.vNormal];
                        std::cout<<"RUNE_WATER_SURFACE name="<<name<<" flags="<<surface.PolyFlags<<" x="<<pos.x<<" y="<<pos.y<<" z="<<pos.z<<" nz="<<normal.z<<'\n';
                    }
                    if(commandline->HasArg("", "--probe-sky-surfaces")) for(auto& surface:level->Model->Surfaces) {
                        if(!surface.Material)continue;
                        std::string name=surface.Material->package->GetPackageName().ToString()+"."+surface.Material->Name.ToString();
                        std::string lower=name;for(char& c:lower)c=(char)std::tolower((unsigned char)c);
                        if(lower.find("sky.")!=0)continue;
                        auto pos=level->Model->Points[surface.pBase],normal=level->Model->Vectors[surface.vNormal];
                        std::cout<<"RUNE_SKY_SURFACE name="<<name<<" flags="<<surface.PolyFlags<<" textureflags="<<surface.Material->PolyFlags()<<" x="<<pos.x<<" y="<<pos.y<<" z="<<pos.z<<" nz="<<normal.z<<'\n';
                    }
                    if(commandline->HasArg("", "--probe-fire-geometry")) for(auto& node:level->Model->Nodes) {
                        auto* m=level->Model;auto& surface=m->Surfaces[node.Surf];
                        if(!surface.Material || surface.Material->Name!="DMflame2")continue;
                        auto normal=m->Vectors[surface.vNormal];
                        std::cout<<"FIRE_FACE surface="<<node.Surf<<" normal="<<normal.x<<","<<normal.y<<","<<normal.z<<" pan="<<surface.PanU<<","<<surface.PanV<<'\n';
                        for(int j=0;j<node.NumVertices;j++) {
                            auto p=m->Points[m->Vertices[node.VertPool+j].Vertex];auto delta=p-m->Points[surface.pBase];
                            std::cout<<"FIRE_VERTEX "<<p.x<<","<<p.y<<","<<p.z<<" uv="<<dot(delta,m->Vectors[surface.vTextureU])+surface.PanU<<","<<dot(delta,m->Vectors[surface.vTextureV])+surface.PanV<<'\n';
                        }
                    }
                    if(commandline->HasArg("", "--probe-effect-surfaces")) {
                        std::set<std::string> seen;
                        for(auto& surface:level->Model->Surfaces) if(surface.Material) {
                            auto* t=surface.Material;
                            auto name=t->package->GetPackageName().ToString()+"."+t->Name.ToString();
                            if(seen.insert(name).second)std::cout<<"RUNE_EFFECT_SURFACE name="<<name<<" class="<<t->Class->Name.ToString()<<" flags="<<surface.PolyFlags<<" textureflags="<<t->PolyFlags()<<'\n';
                            if(t->Name=="DMflame2" && commandline->HasArg("","--probe-fire-image")) {
                                for(int i=0;i<60;i++){t->TextureModified=false;t->Update(.04f);}
                                auto& mip=t->UsedMipmaps.front();Array<uint32_t> rgba(mip.Data.size());
                                for(size_t i=0;i<mip.Data.size();i++)rgba[i]=t->Palette()->Colors[mip.Data[i]]|0xff000000u;
                                size_t size=0;void* png=tdefl_write_image_to_png_file_in_memory_ex(rgba.data(),mip.Width,mip.Height,4,&size,6,0);
                                if(png){File::write_all_bytes(commandline->GetArg("","--probe-fire-image",""),png,size);mz_free(png);}
                                std::cout<<"RUNE_FIRE_SIZE width="<<mip.Width<<" height="<<mip.Height<<'\n';break;
                            }

                        }
                    }
					if(commandline->HasArg("", "--probe-map-inventory"))
						for(auto* actor:level->Actors) if(actor) {
							std::cout<<"RUNE_MAP_ACTOR "<<std::quoted(actor->Name.ToString())<<' '<<std::quoted(actor->Class->Name.ToString())
								<<' '<<std::quoted(actor->Tag().ToString())<<' '<<std::quoted(actor->Event().ToString())
								<<' '<<actor->Location().x<<' '<<actor->Location().y<<' '<<actor->Location().z;
							for(const char* prop:{"URL","NextOrder","NextOrderTag","Orders","OrderTag","OtherTag","AttachTag","InitialState","Physics","bWaitToBeTriggered","bDestroyable","Position","RateModifier","PauseTime","bInterpInstant","bSplineThruPoints","bEndOfPath","bSkipNextPath","StartSpeed","EndSpeed","DrawType","ParticleType","ParticleCount","NumConPts","SystemLifeSpan","bSystemOneShot","bSystemTicks","bEventDeath","bEventSystemInit","bEventSystemTick","bEventParticleTick","SpewerDamage","SpewerLength","SpewerRadius","bAutoStart","MsgLocation","CollisionRadius","CollisionHeight","bBlockPlayers","bCollideActors","bJointsBlock"}) {
								auto* property=actor->Class->FindMemberProperty(prop);
								std::string value;
								if(property && property->ValueType==ExpressionValueType::ValueString) value=actor->GetString(prop);
								else if(property && property->ValueType==ExpressionValueType::ValueName) value=actor->GetName(prop).ToString();
								else if(property && property->ValueType==ExpressionValueType::ValueBool) value=actor->GetBool(prop)?"true":"false";
								else if(property && property->ValueType==ExpressionValueType::ValueInt) value=std::to_string(actor->GetInt(prop));
								else if(property && property->ValueType==ExpressionValueType::ValueByte) value=std::to_string(actor->GetByte(prop));
								else if(property && property->ValueType==ExpressionValueType::ValueFloat) value=std::to_string(actor->GetFloat(prop));
								else if(property && std::string(prop)=="MsgLocation") { auto v=actor->GetVector(prop); value=std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z); }
								std::cout<<' '<<std::quoted(value);
							}
							std::cout<<'\n';
						}

				}
				return 0;
			}
			catch (const std::exception& e) { std::cerr << "RUNE_PROBE_FAILED: " << e.what() << '\n'; return 1; }
		}

		if (commandline->HasArg("", "--direct"))
		{
			Logger::Get()->SetCallback([](const LogMessageLine& line) { std::cerr << line.Text << '\n'; });
			GameFolderSelection::UpdateList();
			if (GameFolderSelection::Games.size()!=1) throw std::runtime_error("Pass exactly one recognized game folder with --direct");
			Engine game(GameFolderSelection::GetLaunchInfo(0));
			game.Run();
			return 0;
		}

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [Path to game folder]\n";
			return 0;
		}

		int selectedGameIndex = commandline->HasArg("", "--legacy-launcher") ? LauncherWindow::ExecModal() : RuneSetup::SelectGame();
		if (selectedGameIndex >= 0)
		{
			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
			Engine engine(info);
			engine.Run();
		}
	}
	catch (const std::exception& e)
	{
#ifdef OPENRUNE_ENABLE_NETWORK
        if (std::any_of(args.begin(),args.end(),[](const std::string& a){return a.starts_with("--rune-server=");}))
            RuneMultiplayer::SetHostFailure(e.what());
#endif
		if (std::find(args.begin(), args.end(), "--direct") != args.end() || std::find(args.begin(), args.end(), "--rune-network-probe") != args.end() || std::any_of(args.begin(),args.end(),[](const std::string& a){return a.starts_with("--rune-network-process-probe") || a.starts_with("--rune-tactics-probe");}) || std::find(args.begin(), args.end(), "--rune-export-textures") != args.end() || std::find(args.begin(), args.end(), "--rune-export-sounds") != args.end() || std::find(args.begin(), args.end(), "--sound-pack-probe") != args.end())
		{
			std::cerr << "DIRECT_LAUNCH_FAILED: " << e.what() << '\n';
			return 1;
		}
		ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
