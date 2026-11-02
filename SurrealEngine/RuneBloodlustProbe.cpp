#include "Precomp.h"
#include "Engine.h"
#include "RuneBloodlust.h"
#include "RuneRushPose.h"
#include "RuneBloodlustAudio.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include <iostream>
void RunRuneBloodlustProbe(Engine& game){
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("bloodlust fixture: ")+why);};
 RuneBloodlust::Enabled=true;RuneDualWield::Enabled=false;RuneTactics::Enabled=false;RuneKick::Enabled=false;RuneBash::Enabled=false;
 auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");
 game.LoadMap(url);game.LoginPlayer();auto* p=game.viewport->Actor();p->bShowMenu()=false;
 auto tick=[&](int n){for(int i=0;i<n;++i){constexpr float dt=1.f/120;game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);}};
 tick(30);p->DisableEvent("PlayerTick");
 // Replay the real DM-Hildir stair side: coarse distance fallback stopped
 // 55-132 units short. These approaches must use the original grab animation.
 {
  for(float y:{1124.f,1200.f,1280.f}){
   p->Bloodlust={};p->GotoState("PlayerWalking","Begin");p->GetUObject("AnimProxy")->GotoState("Idle","Begin");p->SetPhysics(PHYS_Falling);p->Velocity()=p->Acceleration()=vec3(0);p->SetRotation(Rotator(0,-15939,0));
   check(p->SetLocation(vec3(-325,y,-170)),"stair approach placement");p->SetBool("bBloodLust",true);p->SetInt("Strength",p->GetInt("MaxStrength"));p->Bloodlust.tap=game.LevelInfo->TimeSeconds();
   check(!p->TryGrabRuneEdge(2),"grab accepted away from wall");
   check(RuneBloodlust::Leap(p,vec3(.069439,-.997586,0)),"stair leap start");
   std::cout<<"STAIR_LEAP_START y="<<y<<" limit="<<p->Bloodlust.limit<<"\n";
   bool grabbed=false;float high=-170;
   for(int i=0;i<360;++i){tick(1);high=std::max(high,p->Location().z);grabbed=grabbed||p->GetStateName()=="EdgeHanging";}
   check(grabbed && p->GetStateName()=="PlayerWalking" && p->Physics()==PHYS_Walking && p->Location().z>40 && !RuneBloodlust::Busy(p),"stair leap must grab, climb and recover");
   std::cout<<"STAIR_LEAP_RESULT grabbed="<<grabbed<<" highest="<<high<<" at="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" state="<<p->GetStateName().ToString()<<"\n";
  }
 }
 vec3 origin=p->Location(),direction(1,0,0);bool lane=false;
 // Pick a placed arena start with room for a bounded 600-unit leap.
 std::vector<UActor*> starts;for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart"))starts.push_back(a);
 for(auto* a:starts)if(!lane){
  if(!p->SetLocation(a->Location()))continue;p->SetPhysics(PHYS_Walking);p->Velocity()=p->Acceleration()=vec3(0);tick(120);
  if(p->Physics()!=PHYS_Walking)continue;
  for(int i=0;i<16&&!lane;++i){float angle=i*3.14159265f/8;direction={std::cos(angle),std::sin(angle),0};auto end=p->Location()+direction*600.f;vec3 hit,normal;
   lane=p->FastTrace(end,p->Location()) && p->CheckLocation(end,22,42,true).first && p->Trace(hit,normal,end-vec3(0,0,140),end,false,vec3(10,10,0)) && normal.z>.7f;
  }if(lane)origin=p->Location();
 }
 check(lane,"no arena lane");std::cout<<"BLOOD_LANE "<<origin.x<<","<<origin.y<<","<<origin.z<<" jumpZ="<<p->JumpZ()<<" groundSpeed="<<p->GroundSpeed()<<" dir="<<direction.x<<","<<direction.y<<std::endl;
 auto reset=[&](){p->Bloodlust={};p->RuneBashPhase=0;p->RuneKickActive=false;p->Health()=1000;p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);p->GotoState("PlayerWalking","Begin");p->GetUObject("AnimProxy")->GotoState("Idle","Begin");p->SetPhysics(PHYS_Walking);p->Velocity()=p->Acceleration()=vec3(0);check(p->SetLocation(origin),"reset position");p->SetRotation(Rotator::FromVector(direction));p->ViewRotation()=p->Rotation();p->SetBool("bBloodLust",false);p->SetInt("Strength",0);tick(120);};
 auto full=[&](){CallEvent(p,"BoostStrength",{ExpressionValue::IntValue((int)p->GetInt("MaxStrength"))});check(RuneBloodlust::Active(p),"original bloodlust activation");};
 auto give=[&](UPawn* pawn,const char* name){auto* a=pawn->Spawn(game.packages->GetPackage("RuneI")->GetClass(name),{},{},pawn->Location()+vec3(0,0,100),{});check(a,"grant spawn");a->SetFloat("RespawnTime",0);auto* w=CallEvent(a,"SpawnCopy",{ExpressionValue::ObjectValue(pawn)}).ToObject();check(w==a,"grant world copy");CallEvent(pawn,"AcquireInventory",{ExpressionValue::ObjectValue(w)});w->GotoState("Idle","Begin");return w;};
 reset();check(RuneBloodlust::Readiness(p)==0,"inactive cue");full();check(RuneBloodlust::Readiness(p)==1,"leap-only cue without shield");
 give(p,"VikingShield");check(RuneBloodlust::Readiness(p)==3,"jump + shield combo cue");
 p->SetInt("Strength",(p->GetInt("MaxStrength")*49)/100);check(!RuneBloodlust::Readiness(p),"insufficient cost cue");
 reset();full();check(!RuneBloodlust::JumpPressed(p),"first tap must remain ordinary jump");tick(6);p->Acceleration()=direction*100.f;
 check(RuneBloodlust::JumpPressed(p),"second tap leap");check(p->GetInt("Strength")<=p->GetInt("MaxStrength")/2,"leap cost");check(p->Physics()==PHYS_Falling,"leap not airborne");
 check(!RuneBloodlust::Charge(p) && !RuneBloodlust::Readiness(p),"busy move accepted/cue remained");
 float highest=p->Location().z,farthest=0;for(int i=0;i<360;++i){tick(1);highest=std::max(highest,p->Location().z);auto d=p->Location()-origin;d.z=0;farthest=std::max(farthest,length(d));}
 check(!RuneBloodlust::Busy(p),"leap never finished");check(farthest>40 && farthest<680 && highest-origin.z>90 && highest-origin.z<250,"unbounded leap");
 std::cout<<"BLOOD_LEAP_OK distance="<<farthest<<" height="<<highest-origin.z<<std::endl;
 for(int repeat=0;repeat<2;++repeat){
  tick(240);p->SetInt("Strength",p->GetInt("MaxStrength"));p->SetBool("bBloodLust",true);
  check(!RuneBloodlust::JumpPressed(p),"repeat first tap");tick(6);
  check(RuneBloodlust::JumpPressed(p),"repeated leap rejected after landing/cooldown");tick(360);
 }
 check(p->Bloodlust.leaps==3,"repeat leap count");
 auto* loop=UObject::TryCast<USound>(p->GetUObject("BerserkSoundLoop"));auto* jump=UObject::TryCast<USound>(p->GetUObject("WeaponPickupSound"));
 check(loop && IsRuneBloodlustSound(p,loop),"berserk loop not filtered");
 check(jump && !IsRuneBloodlustSound(p,jump),"ordinary pickup sound filtered");
 std::cout<<"BLOOD_REPEAT_AUDIO_OK three leaps, berserk-only sound filter\n";
 reset();full();give(p,"VikingShortSword");
 {auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));auto* weapon=p->Weapon();auto saved=p->Bloodlust;
  auto joint=[&](const char* name){for(size_t i=0;i<model->Joints.size();++i)if(model->Joints[i].Name==name)return (int)i;throw std::runtime_error(name);};
  check(RuneBloodlust::Charge(p),"rush pose start");p->Bloodlust.age=.5f;
  auto bones=model->ActorJointMatrices(p);auto rotation=Coords::Rotation(model->RotationOffset).ToMatrix();
  for(auto name:{"chest","neck","head","lshouldb","rshouldb","lwrist","rwrist","attach_hand","attatch_sword","attach_shielda"}){int j=joint(name);auto pos=(rotation*bones[j]*vec4(0,0,0,1)).xyz();std::cout<<"RUSH_POSE "<<name<<" "<<pos.x<<","<<pos.y<<","<<pos.z<<std::endl;}
  check(length((bones[joint("attach_hand")]*vec4(0,0,0,1)).xyz()-(bones[joint("attatch_sword")]*vec4(0,0,0,1)).xyz())<1,"rush weapon not stowed");
  check(p->Weapon()==weapon && weapon->GetUObject("AttachParent")==p,"rush changed selected inventory");
  auto localPoint=[&](const char* name){return (rotation*bones[joint(name)]*vec4(0,0,0,1)).xyz();};
  check(localPoint("lwrist").x>localPoint("chest").x+10 && localPoint("rwrist").x>localPoint("chest").x+10,"both hands must brace ahead of chest");
  check(length(localPoint("lwrist")-localPoint("rwrist"))<16,"rush hands separated from shield grip");
  auto shieldNormal=(rotation*bones[joint("attach_shielda")]*vec4(0,0,-1,0)).xyz();check(shieldNormal.x>.98f,"shield must face squarely forward");
  RuneBloodlust::Enabled=false;RuneBloodlust::Tick(p,1.f/120);RuneBloodlust::Enabled=true;
  check(!RuneBloodlust::Busy(p) && !p->Bloodlust.rushing && p->Weapon()==weapon,"cancel failed to retain weapon");
  bones=model->ActorJointMatrices(p);
  check(length((bones[joint("attach_hand")]*vec4(0,0,0,1)).xyz()-(bones[joint("attatch_sword")]*vec4(0,0,0,1)).xyz())>5,"cancel left weapon stowed");
  p->Bloodlust=saved;
  std::cout<<"BLOOD_RUSH_POSE_OK stow, two-hand brace, cancellation restore\n";
 }
 reset();full();auto* victim=UObject::TryCast<UPawn>(p->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},origin+direction*130.f,Rotator::FromVector(-direction)));check(victim,"victim spawn");tick(2);
 auto freeze=[&](){victim->Health()=1000;victim->Enemy()=p;victim->GotoState("Fighting","Begin");victim->StateFrame->SetLatentState(LatentRunState::Stop);victim->DisableEvent("FrameNotify");victim->Velocity()=victim->Acceleration()=vec3(0);victim->Bloodlust={};};
 freeze();int health=victim->Health(); std::cout<<"CHARGE_GATE state="<<p->GetStateName().ToString()<<" physics="<<int(p->Physics())<<" ready="<<RuneBloodlust::Readiness(p)<<" strength="<<p->GetInt("Strength")<<" active="<<p->GetBool("bBloodLust")<<" proxy="<<p->GetUObject("AnimProxy")->GetStateName().ToString()<<" shield="<<(p->GetUObject("Shield")!=nullptr)<<" kick="<<p->RuneKickActive<<" bash="<<p->RuneBashPhase<<std::endl;check(RuneBloodlust::Charge(p),"shield charge start");tick(54);check(length(p->Location()-origin)<5,"rush moved before stow and brace finished");
 for(int i=0;i<180;++i){victim->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);}
 std::cout<<"CHARGE_RESULT phase="<<p->Bloodlust.phase<<" state="<<p->GetStateName().ToString()<<" physics="<<int(p->Physics())<<" hits="<<p->Bloodlust.hits<<" damage="<<health-victim->Health()<<" travel="<<p->Bloodlust.travel<<std::endl;check(p->Bloodlust.hits==1 && victim->Health()<health,"single charge impact");check(!RuneBloodlust::Busy(p),"charge never finished");check(p->Bloodlust.charges==1,"charge counter");
 std::cout<<"BLOOD_CHARGE_OK hits="<<p->Bloodlust.hits<<" damage="<<health-victim->Health()<<" travel="<<p->Bloodlust.travel<<std::endl;
 reset();full();check(victim->SetLocation(origin+direction*130.f),"blocked target placement");freeze();give(victim,"VikingShield");CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(true)});victim->SetRotation(Rotator::FromVector(-direction));
 check(RuneBloodlust::Charge(p),"blocked charge start");for(int i=0;i<180;++i){victim->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);}
 check(p->Bloodlust.hits==1 && p->Bloodlust.travel<150,"raised shield did not stop charge");std::cout<<"BLOOD_BLOCK_OK travel="<<p->Bloodlust.travel<<std::endl;
 victim->Destroy();reset();full();
 auto* wall=p->Spawn(game.packages->GetPackage("RuneI")->GetClass("Stool"),{},{},p->Location()+direction*130.f,{});check(wall,"wall fixture");wall->SetPhysics(PHYS_None);wall->SetCollisionSize(35,128);wall->SetCollision(true,true,true);
 check(RuneBloodlust::Charge(p),"wall charge start");tick(180);check(!RuneBloodlust::Busy(p)&&p->Bloodlust.hits==0&&p->Bloodlust.travel<110,"charge crossed solid obstacle");wall->Destroy();std::cout<<"BLOOD_WALL_OK travel="<<p->Bloodlust.travel<<std::endl;
 // Isolated collision fixtures: grate lip, stairs in both directions, and
 // the already-tested full-height wall. These are real blocking actors.
 auto platform=[&](float along,float top,float radius){
  auto* a=p->Spawn(game.packages->GetPackage("RuneI")->GetClass("Stool"),{},{},origin+direction*along+vec3(0,0,100),{});check(a,"step spawn");
  a->SetPhysics(PHYS_None);a->SetCollision(false,false,false);a->SetCollisionSize(radius,top*.5f);
  check(a->SetLocation(origin+direction*along+vec3(0,0,-p->CollisionHeight()+top*.5f+.2f)),"step placement");
  a->SetCollision(true,true,true);return a;
 };
 reset();full();check(RuneBloodlust::Charge(p),"open rush");tick(240);
 check(p->Bloodlust.travel>555 && p->Bloodlust.travel<562,"longer rush distance");
 std::cout<<"BLOOD_RUSH_RANGE_OK travel="<<p->Bloodlust.travel<<"\n";
 reset();auto* lip=platform(140,8,55);full();check(RuneBloodlust::Charge(p),"grate lip rush");float climbed=0;
 for(int i=0;i<240;++i){tick(1);climbed=std::max(climbed,p->Location().z-origin.z);}
 check(p->Bloodlust.travel>550 && climbed>6,"rush stopped on low grate lip");lip->Destroy();
 std::cout<<"BLOOD_RUSH_GRATE_OK travel="<<p->Bloodlust.travel<<" climb="<<climbed<<"\n";
 reset();std::vector<UActor*> steps;for(int i=0;i<3;++i)steps.push_back(platform(90+i*65,12.f*(i+1),45));
 full();check(RuneBloodlust::Charge(p),"stair rush");climbed=0;
 for(int i=0;i<240;++i){tick(1);climbed=std::max(climbed,p->Location().z-origin.z);}
 check(p->Bloodlust.travel>215 && climbed>33,"rush stopped climbing walkable stairs");
 std::cout<<"BLOOD_RUSH_STAIRS_UP_OK travel="<<p->Bloodlust.travel<<" climb="<<climbed<<"\n";
 reset();check(p->SetLocation(origin+direction*220.f+vec3(0,0,36.5f)),"descending stair start");tick(60);p->SetRotation(Rotator::FromVector(-direction));p->ViewRotation()=p->Rotation();full();check(RuneBloodlust::Charge(p),"descending stairs rush");
 tick(240);check(p->Bloodlust.travel>215 && p->Location().z<origin.z+4,"rush stopped descending stairs");
 std::cout<<"BLOOD_RUSH_STAIRS_DOWN_OK travel="<<p->Bloodlust.travel<<"\n";for(auto* a:steps)a->Destroy();
 reset();full();RuneBloodlust::Enabled=false;check(!RuneBloodlust::Readiness(p) && !RuneBloodlust::Charge(p) && !RuneBloodlust::JumpPressed(p),"toggle off");RuneBloodlust::Enabled=true;
 // Selected humanoids acquire rage through real damage, and retain wind-up.
 UPawn* elite=nullptr;for(int i=0;i<6&&!elite;++i){auto* q=UObject::TryCast<UPawn>(p->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},origin+direction*250.f,Rotator::FromVector(-direction)));if(q && RuneBloodlust::Eligible(q))elite=q;else if(q)q->Destroy();}
 check(elite,"eligible elite");tick(60);elite->Health()=1000;{auto* parts=static_cast<int*>(elite->GetProperty("BodyPartHealth"));for(int i=0;i<15;++i)parts[i]=10000;}elite->Enemy()=p;elite->GotoState("Fighting","Begin");elite->StateFrame->SetLatentState(LatentRunState::Stop);
 CallEvent(elite,"JointDamaged",{ExpressionValue::IntValue(150),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(elite->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
 std::cout<<"ENEMY_RAGE health="<<elite->Health()<<" rage="<<elite->Bloodlust.rage<<" hidden="<<elite->bHidden()<<" eligible="<<RuneBloodlust::Eligible(elite)<<std::endl;check(elite->Bloodlust.active,"enemy rage did not activate");
 elite->Enemy()=p;elite->Bloodlust.think=100;elite->GotoState("Fighting","Begin");elite->SetPhysics(PHYS_Walking);elite->SetBool("bSwingingHigh",false);elite->SetBool("bSwingingLow",false);give(elite,"VikingShield");
 check(RuneBloodlust::Charge(elite),"enemy charge");auto start=elite->Location();for(int i=0;i<30;++i){elite->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);}check(length(elite->Location()-start)<5,"enemy windup too short");
 for(int i=0;i<210;++i){elite->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);}
 check(elite->Bloodlust.charges==1 && !RuneBloodlust::Busy(elite),"enemy charge did not complete");
 elite->Bloodlust={};elite->Bloodlust.active=true;elite->Bloodlust.rage=100;elite->Bloodlust.think=100;elite->GotoState("Fighting","Begin");elite->SetPhysics(PHYS_Walking);elite->SetLocation(origin+direction*250.f);elite->SetBool("bSwingingHigh",false);elite->SetBool("bSwingingLow",false);
 elite->StateFrame->SetLatentState(LatentRunState::Stop);reset();check(RuneBloodlust::Leap(elite,-direction),"enemy leap");for(int i=0;i<240;++i){elite->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);}check(elite->Bloodlust.leaps==1&&!RuneBloodlust::Busy(elite),"enemy leap did not complete");
 reset();elite->Bloodlust={};elite->Bloodlust.active=true;elite->Bloodlust.rage=100;elite->Bloodlust.think=0;elite->Tactics={};elite->Enemy()=p;elite->GotoState("Fighting","Begin");elite->StateFrame->SetLatentState(LatentRunState::Stop);elite->SetPhysics(PHYS_Walking);check(elite->SetLocation(origin+direction*250.f),"AI charge position");elite->Bloodlust.active=false;elite->Acceleration()=elite->Velocity()=vec3(0);tick(120);check(elite->Physics()==PHYS_Walking,"AI fixture must land first");elite->GotoState("Fighting","Begin");elite->StateFrame->SetLatentState(LatentRunState::Stop);elite->SetRotation(Rotator::FromVector(-direction));elite->SetBool("bSwingingHigh",false);elite->SetBool("bSwingingLow",false);elite->Bloodlust.active=true;elite->Bloodlust.rage=100;elite->Bloodlust.think=0;RuneTactics::Enabled=true;
 tick(240);check(elite->Bloodlust.charges>=1,"tactical enemy did not choose bloodlust charge");RuneTactics::Enabled=false;elite->Destroy();std::cout<<"BLOOD_AI_DECISION_OK\n";
 std::cout<<"BLOOD_ENEMY_OK rage gain, charge windup, charge and leap completion\n";
 reset();full();check(RuneBloodlust::Charge(p),"save charge start");tick(54);auto savedWeaponName=p->Weapon()->Name;game.SaveGameToSlot(94,"Bloodlust isolated fixture");game.LoadFromSaveFile(UnrealURL("?load=94"));game.PossessSavedPlayer();p=game.viewport->Actor();check(!RuneBloodlust::Busy(p) && !p->Bloodlust.rushing,"saved charge resumed pending impacts");check(p->Weapon() && p->Weapon()->Name==savedWeaponName && p->Weapon()->GetUObject("AttachParent")==p,"saved rush lost selected weapon");
 std::cout<<"BLOOD_RUNTIME_OK gates, leap, charge, shield stop, enemy rage, save cancellation\n";
}
