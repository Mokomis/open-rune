#include "Precomp.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "RuneDualWield.h"
#include "RuneFinisher.h"
#include "RuneBattleLaws.h"
#include "RunePhysics.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneTactics.h"
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
#include "VM/Frame.h"
#include <iostream>
#include <stdexcept>
void RunRuneDualWieldProbe(Engine& game) {
 auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(std::string("dual fixture: ")+why);};
 RuneDualWield::Enabled=true;RuneTactics::Enabled=false;RuneKick::Enabled=false;RuneBash::Enabled=false;
 auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");
 game.LoadMap(url);game.LoginPlayer();auto* player=game.viewport->Actor();player->bShowMenu()=false;
 auto tick=[&](int n){for(int i=0;i<n;++i){constexpr float dt=1.f/120;game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);}};
 tick(60);auto origin=player->Location();vec3 forward;bool found=false;
 for(int i=0;i<16&&!found;++i){
  float a=i*3.14159265f/8;forward={std::cos(a),std::sin(a),0};found=true;
  for(float distance:{45.f,65.f,85.f,150.f,160.f}) {auto end=origin+forward*distance;auto placement=player->CheckLocation(end,22,42,true);found=found && placement.first && length(placement.second-end)<.5f && player->FastTrace(end,origin);}
 }
 check(found,"clear fixture lane");
 auto* enemy=UObject::TryCast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},origin+forward*150.f,Rotator::FromVector(-forward)));check(enemy,"enemy spawn");tick(60);
 player->DisableEvent("PlayerTick");enemy->Dual.spawnPending=false;
 enemy->SetCollision(true,false,false);player->SetCollision(true,false,false);
 auto give=[&](UPawn* p,const char* name,bool acquire){
  auto* item=p->Spawn(game.packages->GetPackage("RuneI")->GetClass(name),{},{},p->Location()+vec3(0,0,100),{});check(item,"item spawn");
  item->SetFloat("RespawnTime",0);
  auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();check(copy==item,"grant left a respawning world copy");
  if(acquire)CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
  copy->GotoState("Active","Begin");return UObject::TryCast<UWeapon>(copy);
 };
 auto reset=[&](float distance){
  RuneTactics::Enabled=false;
  for(auto* p:{static_cast<UPawn*>(player),enemy}) {
   p->Health()=10000;auto* health=static_cast<int*>(p->GetProperty("BodyPartHealth"));for(int j=0;j<15;++j)health[j]=10000;
   p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);p->RuneKickActive=p->RuneKickPending=false;p->RuneBashPhase=0;p->RuneBashPending=false;
   p->Dual.lastWholeBody=-1;p->Dual.guard=false;p->Dual.recovery=0;p->Dual.guardBlend=0;p->Tactics={};
   p->Velocity()=p->Acceleration()=vec3(0);p->SetPhysics(PHYS_Walking);CallEvent(p,"WeaponDeactivate");
  }
  player->GotoState("PlayerWalking","Begin");player->SetByte("bAltFire",0);player->SetByte("bFire",0);
  player->GetUObject("AnimProxy")->GotoState("Idle","Begin");
  enemy->Enemy()=nullptr;enemy->GotoState("Waiting","Begin");
  check(player->SetLocation(origin),"player placement");check(enemy->SetLocation(origin+forward*distance),"enemy placement");
  player->SetRotation(Rotator::FromVector(forward));player->ViewRotation()=player->Rotation();enemy->SetRotation(Rotator::FromVector(-forward));
 };
 reset(150);
 give(player,"VikingShortSword",true);auto* off=give(player,commandline->HasArg("","--fin-campaign")?"RomanSword":"HandAxe",false);
 check(RuneDualWield::Equip(player,off),"player equip");
 give(enemy,"VikingShortSword",true);auto* enemyOff=give(enemy,commandline->HasArg("","--fin-campaign")?"RomanSword":"HandAxe",false);
 check(RuneDualWield::Equip(enemy,enemyOff),"Viking equip");
 check(!RuneDualWield::OneHanded(give(player,"DwarfBattleSword",false)),"two handed exclusion");
 std::cout<<"DUAL_EQUIPMENT_OK player="<<player->Weapon()->Name.ToString()<<" left="<<off->Name.ToString()<<" enemy="<<enemyOff->Name.ToString()<<std::endl;

 if(commandline->HasArg("","--fin-campaign")){
  RuneBattleLaws::Campaign=RuneBattleLaws::All;
  auto species=commandline->GetArg("","--fin-species","");
  if(!species.empty()){
    reset(180);
    auto* type=game.packages->GetPackage("RuneI")->GetClass(species);
    check(type,"species class");
    auto* creature=UObject::TryCast<UPawn>(player->Spawn(type,{},{},origin+forward*50.f,Rotator::FromVector(forward)));
    check(creature,"species spawn");creature->Health()=1000;creature->SetRotation(Rotator::FromVector(forward));
    auto* weapon=player->Weapon();CallEvent(weapon,"StartAttack");
    CallEvent(weapon,"DoWeaponSwipe",{ExpressionValue::ObjectValue(creature),ExpressionValue::IntValue(1),ExpressionValue::IntValue(0),
        ExpressionValue::VectorValue(creature->Location()),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(forward*100.f)});
    int bodies=0,physical=0;
    for(auto* b:game.Level->Actors)if(b && !b->bHidden() && !b->bDeleteMe() && b->IsA("Carcass")){
        ++bodies;physical+=RunePhysics::Controls(b);
    }
    std::cout<<"FIN_CREATURE species="<<species<<" hp="<<creature->Health()<<" bodies="<<bodies<<" physical="<<physical<<" state="<<creature->GetStateName().ToString()<<std::endl;
    bool boss=creature->HasProperty("bIsBoss") && creature->GetBool("bIsBoss");
    if(boss)check(creature->Health()>0 && bodies==0 && !RuneFinisher::Hands(player),"boss retains authored damage and survival");
    else check(creature->Health()<=0 && bodies==1 && physical==1,"species immediate physical death");
    if(!boss){
        tick(90);
        bool continuing=false;for(auto* b:game.Level->Actors)if(b && !b->bDeleteMe() && b->IsA("Carcass") && RunePhysics::Controls(b))
            continuing=std::isfinite(b->Location().x+b->Location().y+b->Location().z) && length(b->Location()-origin)<1000;
        check(continuing,"creature ragdoll stays finite and in the arena");
    }
    return;
  }
  auto scenario=commandline->GetArg("","--fin-campaign","player");
  bool goblinAttack=scenario=="goblin";
  bool npc=scenario=="npc" || scenario=="npc-duel" || goblinAttack;
  UPawn* attacker=npc?enemy:static_cast<UPawn*>(player);
  UPawn* victim=npc?static_cast<UPawn*>(player):enemy;
  reset(45);
  if(scenario=="npc-duel" || goblinAttack){
    check(player->SetLocation(origin+forward*145.f),"clear lane for NPC duel");
    victim=UObject::TryCast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("GoblinWarrior"),{},{},origin,{}));
    check(victim,"spawn second campaign NPC");
    victim->SetCollision(true,false,false);victim->Health()=10000;victim->GotoState("Waiting","Begin");
    victim->DisableEvent("SeePlayer");victim->DisableEvent("Bump");victim->DisableEvent("HearNoise");
    victim->SetPhysics(PHYS_None);
    if(goblinAttack){
      std::swap(attacker,victim);
      give(attacker,"VikingShortSword",true);
      attacker->SetPhysics(PHYS_Walking);
    }
  }
  auto yaw=Rotator::FromVector(npc && !goblinAttack?-forward:forward);
  attacker->SetRotation(yaw); victim->SetRotation(yaw);
  if(npc){attacker->Enemy()=victim;attacker->GotoState("Fighting","Begin");RuneTactics::Enabled=true;}
  else {enemy->DisableEvent("SeePlayer");enemy->DisableEvent("Bump");enemy->DisableEvent("HearNoise");
        enemy->SetPhysics(PHYS_None);CallEvent(player->GetUObject("AnimProxy"),"Attack");}
  bool physical=false;
  const bool checkHold=commandline->HasArg("","--pin-campaign");
  for(int i=0;i<900 && victim->Health()>0;++i){
   if(checkHold && attacker->Weapon()->GetStateName()=="Swinging"){
     // Isolate the hold lifecycle at confirmed torso contact. The ordinary
     // campaign scenarios above use the unfiltered weapon sweep.
     auto* rig=UObject::Cast<USkelModel>(GetRuneDisplayMesh(victim));
     int chest=-1;
     for(auto name:{"chest","spinec","torso","base_torso"}){
       for(size_t j=0;j<rig->Joints.size() && j<31;++j)if(rig->Joints[j].Name==name)chest=int(j);
       if(chest>=0)break;
     }
     check(chest>=0,"campaign torso contact joint");
     CallEvent(attacker->Weapon(),"DoWeaponSwipe",{ExpressionValue::ObjectValue(victim),
         ExpressionValue::IntValue(1u<<chest),ExpressionValue::IntValue(0),ExpressionValue::VectorValue(victim->Location()),
         ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(forward*100.f)});
   }
   if(!npc || scenario=="npc-duel" || goblinAttack)victim->StateFrame->SetLatentState(LatentRunState::Stop);
   victim->SetRotation(yaw);victim->DesiredRotation()=yaw;if(victim==player)player->ViewRotation()=yaw;tick(1);
  }
  int count=0;
  for(auto* body:game.Level->Actors)if(body && !body->bDeleteMe() && !body->bHidden() && body->IsA("RuneCarcass")){
     ++count;physical|=RunePhysics::Controls(body);
  }
  std::cout<<"FIN_CAMPAIGN npc="<<npc<<" victim="<<victim->Class->Name.ToString()<<" hp="<<victim->Health()<<" bodies="<<count<<" ragdoll="<<physical<<" hands="<<RuneFinisher::Hands(attacker)<<std::endl;
  check(victim->Health()<=0 && RuneFinisher::Hands(attacker)==(goblinAttack?1:3) && physical && count==1,"campaign paired finish and immediate single ragdoll");
  if(checkHold){
    check(attacker->finisherHeld,"torso hit holds the campaign corpse");
    UActor* corpse=nullptr;for(auto* a:game.Level->Actors)if(a && RunePhysics::Impaled(a))corpse=a;
    check(corpse,"articulated campaign body is on the swords");
    tick(150);
    check(attacker->finisherHeld!=npc,"campaign AI withdraws while the player can keep holding");
    if(!npc)check(RunePhysics::ReleaseImpale(attacker),"campaign player withdraws");
    check(!RunePhysics::Impaled(corpse) && RunePhysics::Controls(corpse),"same free ragdoll after campaign withdrawal");
    std::cout<<"CAMPAIGN_PIN_OK npc="<<npc<<std::endl;
  }
  return;
 }
 if(commandline->HasArg("","--probe-dual-motion")) {
  auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(player));
  for(auto& j:m->Joints)std::cout<<"DUAL_JOINT name="<<j.Name.ToString()<<" parent="<<j.Parent<<" group="<<j.Group<<" flags="<<j.Flags<<'\n';
 }
 // Original proxy attack state supplies speed, frame notifications and real sweeps.
 int rightDamage=0,leftDamage=0;float maxRecoveryStep=0;
 for(int attack=0;attack<6;++attack) {
  reset(45);tick(1);enemy->GotoState("Waiting","Begin");enemy->DisableEvent("FrameNotify");enemy->DisableEvent("SeePlayer");enemy->DisableEvent("HearNoise");enemy->DisableEvent("Bump");enemy->StateFrame->SetLatentState(LatentRunState::Stop);enemy->SetPhysics(PHYS_None);
  auto* proxy=player->GetUObject("AnimProxy");int before=enemy->Health();
  CallEvent(proxy,"Attack");
  Array<mat4> previousBones;
  for(int frame=0;frame<150;++frame) {enemy->StateFrame->SetLatentState(LatentRunState::Stop);tick(1);
   {
    auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(player));auto bones=m->ActorJointMatrices(player);
    for(size_t j=0;j<bones.size();j++)if(m->Joints[j].Group==0 && !previousBones.empty()) {
     float delta=length((bones[j]*vec4(0,0,0,1)).xyz()-(previousBones[j]*vec4(0,0,0,1)).xyz());
     auto name=m->Joints[j].Name;
     if(frame>80 && (name=="lankle" || name=="rankle" || name=="ltoe" || name=="rtoe"))maxRecoveryStep=std::max(maxRecoveryStep,delta);
     if(commandline->HasArg("","--probe-dual-motion") && delta>3)std::cout<<"DUAL_LEG_JUMP attack="<<attack<<" frame="<<frame<<" joint="<<m->Joints[j].Name.ToString()<<" delta="<<delta<<" left="<<player->Dual.left<<" body="<<player->AnimSequence().ToString()<<" upper="<<UObject::Cast<UActor>(proxy)->AnimSequence().ToString()<<" phase="<<player->AnimFrame()<<" upperphase="<<UObject::Cast<UActor>(proxy)->AnimFrame()<<'\n';
    }
    previousBones=bones;
   }
   if(attack<2 && frame%15==0)std::cout<<"DUAL_FRAME "<<frame<<" seq="<<UObject::Cast<UActor>(proxy)->AnimSequence().ToString()<<" phase="<<UObject::Cast<UActor>(proxy)->AnimFrame()<<" left="<<player->Dual.left<<" primary="<<player->Weapon()->GetStateName().ToString()<<" off="<<off->GetStateName().ToString()<<" notifies="<<proxy->GetBool("bFrameNotifies")<<" distance="<<length(enemy->Location()-player->Location())<<" target="<<enemy->GetStateName().ToString()<<" pz="<<player->Location().z<<" ez="<<enemy->Location().z<<std::endl;}

  int damage=before-enemy->Health();if(attack%2)leftDamage+=damage;else rightDamage+=damage;
  std::cout<<"DUAL_STRIKE n="<<attack<<" damage="<<damage<<" right="<<player->Dual.rightSwings<<" left="<<player->Dual.leftSwings<<" proxy="<<proxy->GetStateName().ToString()<<std::endl;
 }
 std::cout<<"DUAL_RECOVERY_STEP max="<<maxRecoveryStep<<'\n';
 check(maxRecoveryStep<5.f,"feet pop when mirrored return animation meets torso idle");
 check(player->Dual.rightSwings>=2 && player->Dual.leftSwings>=2,"alternating committed swings");
 check(rightDamage>0 && leftDamage>0,"both physical weapons must hit");
 // Both mirrored hands retain fractional animation sampling.
 reset(150);player->Dual.lastWholeBody=-1;auto* proxyActor=UObject::Cast<UActor>(player->GetUObject("AnimProxy"));
 auto savedSequence=proxyActor->AnimSequence();float savedFrame=proxyActor->AnimFrame();auto savedBodySequence=player->AnimSequence();float savedBodyFrame=player->AnimFrame();
 proxyActor->AnimSequence()=player->Weapon()->GetName("A_AttackStandA");proxyActor->bAnimLoop()=false;
 auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(player));
 for(bool left:{false,true}) {
  player->Dual.left=left;auto* weapon=left?player->Dual.offhand:player->Weapon();
  auto grip=[&](float phase){proxyActor->AnimFrame()=phase;auto bones=model->ActorJointMatrices(player);return (RuneDualWield::Attachment(*model,player,weapon,bones)*vec4(0,0,0,1)).xyz();};
  auto a=grip(.30f),b=grip(.31f),mid=grip(.305f);
  check(length(b-a)>.001f && length(mid-a)>.0001f && length(mid-b)>.0001f,"hand pose snapped to keyframe");
 }
 // Independent body/proxy clocks must not splice two phases of a standing spin.
 // Hold the attack clock fixed while moving the unrelated body clock, including tweening.
 for(bool left:{false,true})for(auto field:{"A_AttackStandA","A_AttackStandB"}) {
  auto seq=player->Weapon()->GetName(field);if(seq.IsNone())continue;
  player->Dual.left=left;player->Dual.lastWholeBody=-1;
  proxyActor->AnimSequence()=player->AnimSequence()=seq;proxyActor->AnimFrame()=.37f;
  player->AnimFrame()=.37f;auto coherent=model->ActorJointMatrices(player);
  for(float phase:{-.01f,.05f,.73f}) {
   player->AnimFrame()=phase;auto actual=model->ActorJointMatrices(player);
   for(size_t j=0;j<actual.size();++j)for(int k=0;k<16;++k)
    check(std::abs(actual[j][k]-coherent[j][k])<.005f,"standing spin mixes body and proxy animation clocks");
  }
 }
 // Starting a right strike must not inherit the previous left spin's hip reflection.
 player->Dual.left=false;player->Dual.lastWholeBody=-1;
 proxyActor->AnimSequence()=player->AnimSequence()=player->Weapon()->GetName("A_AttackStandA");
 proxyActor->AnimFrame()=player->AnimFrame()=.25f;
 // Compare the same paired attack on both sides of the transition.
 player->Dual.followAvailable=player->Dual.followPrimed;
 auto cleanRight=model->ActorJointMatrices(player);
 player->Dual.lastWholeBody=game.TotalTime;player->Dual.nextLeft=false;
 RuneDualWield::BeginSwing(player);
 auto afterLeft=model->ActorJointMatrices(player);
 for(size_t j=0;j<cleanRight.size();++j)for(int k=0;k<16;++k)
  check(std::abs(cleanRight[j][k]-afterLeft[j][k])<.005f,"right strike inherits mirrored hips from preceding left strike");
 player->Dual.lastWholeBody=-1;
 std::cout<<"DUAL_PHASE_COHERENCE_OK both hands and standing spins use one clock\n";
 // Validate the rendered calf mesh, not only bone lengths: opposite legs
 // have different skinning bases, so a raw mirrored pose can stretch skin edges.
 auto& geometry=GetRuneGeometry(player)->Meshes[player->GetByte("SkelMesh")];
 auto lowerLeg=[&](int joint) {
  while(joint>=0) {auto n=model->Joints[joint].Name;if(n=="lhip" || n=="rhip")return true;joint=model->Joints[joint].Parent;}
  return false;
 };
 auto skin=[&](const Array<mat4>& bones,const RuneVertex& v) {
  auto point=(bones[v.Joint[0]]*vec4(v.Point[0],1)).xyz()*v.Weight;
  if(v.Weight!=1)point+=(bones[v.Joint[1]]*vec4(v.Point[1],1)).xyz()*(1-v.Weight);
  return point;
 };
 float maxCalfStretch=0;int calfEdges=0;
 for(auto field:{"A_AttackStandA","A_AttackStandB"})for(int frame=0;frame<=20;++frame) {
  auto seq=player->Weapon()->GetName(field);if(seq.IsNone())continue;
  player->AnimSequence()=proxyActor->AnimSequence()=seq;
  player->AnimFrame()=proxyActor->AnimFrame()=frame*.0475f;player->Dual.lastWholeBody=-1;
  player->Dual.left=false;auto normal=model->ActorJointMatrices(player);
  player->Dual.left=true;auto mirror=model->ActorJointMatrices(player);
  for(const auto& t:geometry.Triangles) {
   bool calf=true;
   for(int k=0;k<3;++k) {const auto& v=geometry.Vertices[t.Index[k]];calf=calf && lowerLeg(v.Joint[0]) && (v.Weight==1 || lowerLeg(v.Joint[1]));}
   if(!calf)continue;
   for(int k=0;k<3;++k) {
    const auto& a=geometry.Vertices[t.Index[k]];const auto& b=geometry.Vertices[t.Index[(k+1)%3]];
    float original=length(skin(normal,a)-skin(normal,b));
    float mirrored=length(skin(mirror,a)-skin(mirror,b));
    if(original<=.1f)continue;
    maxCalfStretch=std::max(maxCalfStretch,mirrored/original);++calfEdges;
   }
  }
 }
 check(calfEdges>100,"calf skin regression did not exercise the mesh");
 std::cout<<"DUAL_CALF_SKIN max_stretch="<<maxCalfStretch<<" edges="<<calfEdges<<'\n';
 check(maxCalfStretch<5.f,"mirrored strike stretches calf skin despite valid bone lengths");
 // A whole-body left strike must reflect the hips and torso together, not only the torso.
 for(auto field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackStandA"})for(float phase:{.1f,.3f,.5f,.7f,.9f}) {
  auto seq=player->Weapon()->GetName(field);if(seq.IsNone())continue;
  proxyActor->AnimSequence()=seq;proxyActor->AnimFrame()=phase;player->AnimSequence()=seq;player->AnimFrame()=phase;
  player->Dual.left=false;auto right=model->ActorJointMatrices(player);
  player->Dual.left=true;auto left=model->ActorJointMatrices(player);
  for(size_t j=0;j<model->Joints.size();++j)if(model->Joints[j].Name=="chest" || model->Joints[j].Name=="pelvis" || model->Joints[j].Name=="neck")
   {auto reflect=mat4::scale(-1,1,1);auto expected=reflect*right[j]*reflect;for(int k=0;k<16;++k)check(std::abs(left[j][k]-expected[k])<.005f,"left spin torso and hips disagree");}
 }
 std::cout<<"DUAL_SPIN_OK authored central skeleton preserved across both hands\n";
 proxyActor->AnimSequence()=savedSequence;proxyActor->AnimFrame()=savedFrame;player->AnimSequence()=savedBodySequence;player->AnimFrame()=savedBodyFrame;player->Dual.left=false;
 std::cout<<"DUAL_INTERPOLATION_OK both hands sample fractional frames\n";

 // Directional weak defense, including inherited JointDamaged chains exactly once.
 auto damage=[&](UPawn* target,UPawn* attacker){int before=target->Health();CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(20),ExpressionValue::ObjectValue(attacker),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("sever"),ExpressionValue::IntValue(0)});return before-target->Health();};
 reset(85);tick(1);int full=damage(player,enemy);
 reset(85);player->SetByte("bAltFire",1);tick(24);check(RuneDualWield::Blocks(player,enemy),"front crossed guard");int chip=damage(player,enemy);
 check(chip>0 && chip<full,"guard must take chip damage");
 reset(85);player->SetByte("bAltFire",1);tick(24);player->SetRotation(Rotator::FromVector(-forward));check(!RuneDualWield::Blocks(player,enemy),"rear attack blocked");
 std::cout<<"DUAL_GUARD_OK full="<<full<<" chip="<<chip<<std::endl;
 reset(65);player->SetByte("bAltFire",1);tick(24);check(RuneDualWield::Blocks(player,enemy),"kick guard fixture");
 RuneKick::Enabled=true;RuneKick::CommitImpact(enemy,player,false);RuneKick::Enabled=false;
 check(length(player->Velocity().xy())<100 && player->Dual.recovery>0,"crossed kick launches or lacks opening");
 check(!RuneBash::Start(player),"dual wield must not shield bash");
 std::cout<<"DUAL_KICK_OK recoil="<<length(player->RuneKickRecoil)<<std::endl;
 // An intact owned shield is retained, hidden and cold; toggling off restores it.
 reset(150);give(player,"VikingShield",true);tick(24);auto* shield=UObject::TryCast<UActor>(player->GetUObject("Shield"));
 check(shield && RuneDualWield::Equipped(player) && shield->bHidden(),"shield not stowed");
 RuneBash::Enabled=true;player->RuneBashCooldown=0;check(!RuneBash::Start(player),"stowed shield enabled bash");RuneBash::Enabled=false;
 player->Dual.nextLeft=true;CallEvent(player->GetUObject("AnimProxy"),"Attack");tick(20);
 check(player->Dual.left && player->Dual.offhand->GetStateName()=="Swinging","toggle fixture not mid left swing");
 RuneDualWield::Enabled=false;tick(1);check(!player->Dual.offhand && !shield->bHidden(),"shield not restored");
 check(off->GetStateName()!="Swinging" && player->GetUObject("AnimProxy")->GetStateName()=="Idle","toggle retained pending attack");
 RuneDualWield::Enabled=true;tick(2);check(RuneDualWield::Equipped(player),"toggle did not re-equip");
 auto offName=player->Dual.offhand->Name;auto enemyName=enemy->Name;
 player->Dual.nextLeft=true;CallEvent(player->GetUObject("AnimProxy"),"Attack");tick(20);
 check(player->Dual.left,"save fixture not left handed");
 game.SaveGameToSlot(93,"Dual wield isolated fixture");game.LoadFromSaveFile(UnrealURL("?load=93"));game.PossessSavedPlayer();
 player=game.viewport->Actor();enemy=nullptr;for(auto* a:game.Level->Actors)if(a && a->Name==enemyName)enemy=UObject::TryCast<UPawn>(a);
 check(enemy,"saved enemy");tick(2);check(RuneDualWield::Equipped(player) && player->Dual.offhand->Name==offName,"offhand save restoration");
 check(player->Dual.offhand->GetStateName()!="Swinging" && player->GetUObject("AnimProxy")->GetStateName()!="Attacking","save resumed untracked left strike");
 check(!enemy->Dual.spawnPending,"loading must not create new enemy equipment");
 std::cout<<"DUAL_SAVE_OK left="<<offName.ToString()<<std::endl;
 reset(150);auto* thrown=player->Weapon();auto* spare=player->Dual.offhand;CallEvent(player,"Throw");tick(420);
 check(thrown->GetUObject("AttachParent")!=player && player->Dual.offhand!=thrown,"thrown weapon still attached");
 check(player->Weapon()==spare && spare->GetUObject("AttachParent")==player,"throw did not draw remaining weapon");
 check(!RuneDualWield::Equipped(player),"throw retained phantom dual grip");
 std::cout<<"DUAL_THROW_OK\n";
 // Exercise a real Viking attack through tactical behavior with both hands.
 reset(45);player->DisableEvent("PlayerTick");enemy->Enemy()=player;enemy->EnableEvent("SeePlayer");enemy->EnableEvent("FrameNotify");enemy->GotoState("Fighting","Begin");RuneTactics::Enabled=true;
 int before=player->Health();bool pairedCut=false;
 for(int i=0;i<1600;++i){
  tick(1);
  if(enemy->Dual.followAvailable && enemy->Dual.offhand && enemy->Weapon() &&
     enemy->Weapon()->GetStateName()=="Swinging" && enemy->Dual.offhand->GetStateName()=="Swinging")pairedCut=true;
 }
 check(pairedCut,"Viking did not activate a real trailing cut");
 std::cout<<"DUAL_VIKING_FOLLOWUP_OK"<<std::endl;
 std::cout<<"DUAL_VIKING right="<<enemy->Dual.rightSwings<<" left="<<enemy->Dual.leftSwings<<" damage="<<before-player->Health()<<std::endl;
 check(enemy->Dual.rightSwings>0 && enemy->Dual.leftSwings>0,"Viking did not alternate");
 check(player->Health()<before,"Viking did not hit");
 auto* loot=enemy->Dual.offhand;auto* mainLoot=enemy->Weapon();check(loot,"Viking offhand missing before death");
 CallEvent(enemy,"Died",{ExpressionValue::ObjectValue(player),ExpressionValue::NameValue("sever"),ExpressionValue::VectorValue(enemy->Location())});tick(2);
 check(loot->GetUObject("AttachParent")!=enemy && !enemy->Dual.offhand,"death offhand attachment");
 for(int frame=0;frame<600;++frame) {
  tick(1);
  if(frame%120==0)for(auto* w:{mainLoot,loot})std::cout<<"DUAL_LOOT frame="<<frame<<" name="<<w->Name.ToString()<<" state="<<w->GetStateName().ToString()<<" physics="<<(int)w->Physics()<<" owner="<<(w->Owner()?w->Owner()->Name.ToString():"None")<<" attached="<<(w->GetUObject("AttachParent")!=nullptr)<<" carried="<<(bool)w->bCarriedItem()<<" xyz="<<w->Location().x<<","<<w->Location().y<<","<<w->Location().z<<" rot="<<w->Rotation().Pitch<<","<<w->Rotation().Yaw<<","<<w->Rotation().Roll<<" desired="<<w->DesiredRotation().Pitch<<","<<w->DesiredRotation().Yaw<<","<<w->DesiredRotation().Roll<<" vz="<<w->Velocity().z<<std::endl;
 }
 for(auto* w:{mainLoot,loot}) {
  check(w->GetStateName()=="Pickup" && w->Physics()==PHYS_None,"death loot did not settle");
  check(w->Rotation()==w->DesiredRotation(),"settled death loot lost its resting rotation");
  check(!w->Owner() && !w->GetUObject("AttachParent") && !w->bCarriedItem(),"death loot not released");
 }
 // Empty the test player's weapon inventory, then use the real pickup animation
 // on each corpse weapon. Do not confuse duplicate-class rejection with unusable loot.
 RuneDualWield::Unequip(player);player->Dual.drawAfterRelease=nullptr;
 for(auto* item=player->Inventory();item;) {
  auto* next=item->Inventory();
  if(auto* w=UObject::TryCast<UWeapon>(item)) {
   if(w->GetUObject("AttachParent")==player)CallEvent(player,"DetachActorFromJoint",{ExpressionValue::IntValue(w->GetByte("AttachParentJoint"))});
   CallEvent(player,"DeleteInventory",{ExpressionValue::ObjectValue(w)});w->Destroy();
  }
  item=next;
 }
 for(auto* actor:game.Level->Actors)if(auto* w=UObject::TryCast<UWeapon>(actor);w && w!=loot && w!=mainLoot && !w->Owner())w->Destroy();
 for(auto* w:{mainLoot,loot}) {
  w->LifeSpan()=0;
  player->GotoState("PlayerWalking","Begin");player->GetUObject("AnimProxy")->GotoState("Idle","Begin");
  player->Velocity()=player->Acceleration()=vec3(0);player->bFire()=player->bAltFire()=0;
  // Approach from the outside of the two dropped items. A fixed +X offset
  // could put the other item closer, and Use correctly selected that instead.
  auto* other=w==mainLoot?loot:mainLoot;auto away=w->Location()-other->Location();away.z=0;
  away=length(away)>.01f?normalize(away):vec3(1,0,0);
  check(player->SetLocation(w->Location()+away*25.f+vec3(0,0,player->CollisionHeight()-w->CollisionHeight()+1)),"loot pickup position");
  player->SetPhysics(PHYS_Walking);tick(30);
  check(CallEvent(w,"CanBeUsed",{ExpressionValue::ObjectValue(player)}).ToBool(),"corpse loot not usable");
  CallEvent(player,"Use");tick(420);
  check(w->Owner()==player && w->GetUObject("AttachParent")==player,"Use did not recover corpse weapon");
  std::cout<<"DUAL_LOOT_PICKUP_OK weapon="<<w->Name.ToString()<<std::endl;
 }
 auto* dropped=player->Weapon();auto* remaining=player->Dual.offhand;
 check(dropped && remaining,"two recovered weapons not equipped");
 CallEvent(player,"DropWeapon");tick(420);
 check(player->Weapon()==remaining && dropped->Owner()!=player && !dropped->GetUObject("AttachParent"),"drop did not draw spare or retained ghost attachment");
 CallEvent(player,"DropWeapon");tick(420);
 check(!player->Weapon() && !player->Dual.offhand,"last weapon drop left a held weapon");
 auto** children=static_cast<UObject**>(player->GetProperty("JointChild"));
 for(int j=0;j<50;++j)check(!UObject::TryCast<UWeapon>(children[j]),"empty inventory retained a belt/back weapon");
 auto** slots=static_cast<UObject**>(player->GetProperty("StowSpot"));
 for(int j=0;j<3;++j)check(!slots[j],"empty inventory retained a stow reference");
 std::cout<<"DUAL_DROP_OK spare drawn; last drop leaves no carried weapons\n";
 std::cout<<"DUAL_RUNTIME_OK equipment, physical swings, weaker front guard, kick, shield restoration, save/load, throw, Viking, death\n";
}
