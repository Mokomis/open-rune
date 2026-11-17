// Isolated real package/VM/animation/collision/authority fixture.
void Probe(Engine& game){
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("kick counter: ")+why);};
 RuneTactics::Enabled=false;RuneDualWield::Enabled=false;RunePhysics::RequestedMode=0;ScopedRandomSeed random(20261109);
 game.NetworkAuthority=true;auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.LoadMap(url);
 unsigned rigsChecked=0;for(const auto& c:openrune::Characters){auto* defaults=game.packages->FindClass(c.className)->GetDefaultObject<UActor>();if(!anatomy(defaults).valid)throw std::runtime_error(std::string("roster leg geometry: ")+c.className);++rigsChecked;}
 std::cout<<"KICK_COUNTER_ROSTER_OK characters="<<rigsChecked<<std::endl;
 auto* p=game.LoginNetworkPlayer("Sword");auto* target=game.LoginNetworkPlayer("Kicker");check(p&&target,"players");
 p->DisableEvent("PlayerTick");target->DisableEvent("PlayerTick");game.viewport->Actor()=p;
 auto r=anatomy(target);check(r.valid,"leg geometry groups");
 auto tick=[&](int n){for(int f=0;f<n;++f){constexpr float dt=1.f/120;game.TotalTime+=dt;game.LevelInfo->TimeSeconds()+=dt;game.Level->Tick(dt,false);}};
 vec3 origin,dir,wall;check(RunePhysics::FindImpactLane(p,origin,dir,wall),"open lane");origin-=dir*110.f;
 auto give=[&](UPawn* owner,const char* type){auto* item=owner->Spawn(game.packages->FindClass(std::string("RuneI.")+type),{},{},origin+vec3(0,0,100),{});check(item,"weapon spawn");item->SetFloat("RespawnTime",0);auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(owner)}).ToObject();check(copy==item,"weapon copy");CallEvent(owner,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");return UObject::Cast<UActor>(copy);};
 auto* sword=give(p,"VikingShortSword");
 auto reset=[&](float distance=80.f,float angle=0.f){
  for(auto* v:{p,target}){
   RunePhysics::Release(v);v->Health()=1000;auto* parts=static_cast<int*>(v->GetProperty("BodyPartHealth"));for(int j=0;j<15;++j)parts[j]=1000;
   CallEvent(v,"SetDefaultPolygroups");CallEvent(v,"SetDefaultJointFlags");v->GotoState("PlayerWalking","Begin");v->GetUObject("AnimProxy")->GotoState("Idle","Begin");v->SetPhysics(PHYS_Walking);v->SetCollision(true,false,false);v->bFire()=v->bAltFire()=v->bDuck()=0;v->RuneKickActive=v->RuneKickPending=false;v->RuneKickCooldown=0;v->RuneBashPhase=0;v->RuneBashImmunity=0;v->Bloodlust={};v->Velocity()=v->Acceleration()=vec3(0);v->SetBool("bSwingingHigh",false);v->SetBool("bSwingingLow",false);v->LoopAnim("neutral_idle",1,0,0);v->DisableEvent("PlayerTick");
  }
  CallEvent(sword,"FinishAttack");CallEvent(sword,"DisableSwipeTrail");sword->GotoState("Active","Begin");CallEvent(sword,"ClearSwipeArray");
  check(p->SetLocation(origin),"sword placement");vec3 forward=Coords::Rotation(Rotator(0,int(angle*10430.f),0))*dir;
  check(target->SetLocation(origin+forward*distance),"kick placement");p->SetRotation(Rotator::FromVector(forward));p->ViewRotation()=p->Rotation();target->SetRotation(Rotator::FromVector(-dir));target->ViewRotation()=target->Rotation();p->DesiredRotation()=p->Rotation();target->DesiredRotation()=target->Rotation();
  for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && Detached(a))a->Destroy();
 };
 auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(target));
 auto expose=[&]{target->RuneKickActive=target->RuneKickPending=true;target->AnimSequence()="neutral_kick";target->AnimFrame()=.6f;target->bAnimLoop()=false;sword->GotoState("Swinging","");auto b=model->ActorJointMatrices(target);return Point(GetRuneMeshToWorld(target)*b[r.knee[1]]);};
 auto swipe=[&](int joint,vec3 at){return CallEvent(sword,"DoWeaponSwipe",{ExpressionValue::ObjectValue(target),ExpressionValue::IntValue(1u<<joint),ExpressionValue::IntValue(0),ExpressionValue::VectorValue(at),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(vec3(0))});};
 reset();int samples=0;for(int f=0;f<100;++f){auto at=expose();target->AnimFrame()=f/100.f;auto b=model->ActorJointMatrices(target);at=Point(GetRuneMeshToWorld(target)*b[r.knee[1]]);if(Exposure(sword,target,1u<<r.knee[1],0,at).side>=0)++samples;}
 check(samples>0,"exposed leg window");std::cout<<"KICK_COUNTER_WINDOW samples="<<samples<<" of=100 contact_seconds="<<RuneKick::ContactSeconds<<std::endl;
 reset();auto at=expose();std::cout<<"DIRECT_EXPOSURE side="<<Exposure(sword,target,1u<<r.knee[1],0,at).side<<" base="<<sword->GetInt("Damage")<<" owner="<<(sword->Owner()?sword->Owner()->Name.ToString():"none")<<std::endl;swipe(r.knee[1],at);check(Missing(target)==1 && target->Health()>0,"real sword script did not sever");check(!target->RuneKickActive && !RuneKick::Compatible(target),"amputee still kicks");check(!RunePhysics::Controls(target),"counter forced a ragdoll");
 check(SpeedScale(target)==.6f,"hopping speed");unsigned limbs=0;for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && loose(a)>=0){mat4 transform;check(LooseTransform(a,transform),"detached transform");++limbs;}check(limbs==1,"one detached leg");
 reset();at=expose();swipe(11,Point(GetRuneMeshToWorld(target)*model->ActorJointMatrices(target)[11]));check(Missing(target)<0,"torso hit severed leg");
 reset();at=expose();target->AnimFrame()=0;swipe(r.knee[1],at);check(Missing(target)<0,"unextended leg severed");
 reset();at=expose();auto law=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=law&~RuneBattleLaws::Kick;swipe(r.knee[1],at);RuneBattleLaws::Campaign=law;check(Missing(target)<0,"disabled kick law still counters");
 reset();at=expose();int damage=sword->GetInt("Damage");sword->SetInt("Damage",1);swipe(r.knee[1],at);sword->SetInt("Damage",damage);check(Missing(target)<0,"weak scratch severed leg");
 std::cout<<"KICK_COUNTER_CONTACT_OK sever=1 no_forced_knockdown=1 body_miss=1 folded_miss=1 law=1 threshold=1"<<std::endl;
 // Real attack animations and native swept weapon collision, no injected hits.
 unsigned natural=0,practical=0,preContact=0,trials=0;int winningDelay=0;float winningDistance=0;
 for(float distance:{65.f,80.f,95.f})for(int delay=-36;delay<=24;delay+=6){
  reset(distance);++trials;bool kicked=false,attacked=false;
  for(int frame=0;frame<220;++frame){
   if(frame==50+std::max(0,-delay)){kicked=RuneKick::Start(target);}
   if(frame==50+std::max(0,delay)){CallEvent(p->GetUObject("AnimProxy"),"Attack");attacked=true;}
   tick(1);if(Missing(target)>=0){++natural;if(distance<=80){++practical;winningDelay=delay;winningDistance=distance;}if(p->Health()==1000)++preContact;break;}
  }

 }
 std::cout<<"KICK_COUNTER_REAL_SWINGS trials="<<trials<<" successes="<<natural<<" in_range="<<practical<<" before_kick_contact="<<preContact<<" delay_ticks="<<winningDelay<<" distance="<<winningDistance<<std::endl;
 check(natural>0 && practical>0,"ordinary sword cannot counter an in-range kick");
 reset();at=expose();swipe(r.knee[1],at);auto start=target->Location();{
  RuneNetworkAuthority movement(game);check(movement.bind(9,target,1),"movement bind");target->EnableEvent("PlayerTick");target->SetPhysics(PHYS_Walking);
  for(int i=0;i<100;++i){movement.beginTick(i+1,2000+i*17);openrune::net::Input input;input.life=1;input.sequence=i+1;input.createdMs=2000+i*17;input.forward=-1000;input.yaw=uint16_t(target->Rotation().Yaw);check(movement.input(9,input),"hopping input");tick(1);}
  movement.unbind(9);target->DisableEvent("PlayerTick");
 }check(length(target->Location()-start)>8,"amputee cannot move");check(Missing(target)>=0,"hopping lost injury");
 auto pose=model->SampleAnimation(target);ApplyPose(*model,target,pose);for(const auto& j:pose)check(std::isfinite(j.Position.x+j.Position.y+j.Position.z),"hopping pose invalid");
 std::cout<<"KICK_COUNTER_HOP_OK distance="<<length(target->Location()-start)<<" physics="<<int(target->Physics())<<std::endl;
 #ifdef OPENRUNE_ENABLE_NETWORK
 reset();{
  RuneNetworkAuthority authority(game);check(authority.bind(1,p,1)&&authority.bind(2,target,1),"authority bind");authority.beginTick(1,1000);at=expose();swipe(r.knee[1],at);check(Missing(target)<0,"grace window skipped");
  target->RuneKickActive=false;target->AnimSequence()="neutral_idle";target->AnimFrame()=0;
  authority.beginTick(3,1034);authority.commitContacts();check(Missing(target)==1,"original contact exposure lost in grace window");
  openrune::net::World world;check(authority.capture(world),"capture");RuneNetworkReplica replica;
  for(const auto& event:authority.pendingActors()){if(event.operation==openrune::net::ActorOperation::Spawn)check(replica.spawn(game,event),"replica spawn");else if(event.operation==openrune::net::ActorOperation::Appearance)check(replica.appearance(game,event),"replica appearance");}
  check(replica.install(openrune::net::Snapshot{1,0,3,world}),"replica install");auto* remote=replica.actor(authority.identity(target));check(remote && Missing(remote)==1,"remote hopping state");
  check(!RuneKick::Compatible(UObject::Cast<UPawn>(remote)),"replica amputee eligibility");std::cout<<"KICK_COUNTER_NETWORK_OK contact_wait_ms=34 original_pose=1 appearance=1"<<std::endl;
 }
 #endif
 // Real campaign ScriptPawn and player roles both use the same committed-hit hook.
 game.NetworkAuthority=false;auto campaign=game.GetDefaultURL("DM-Hildir.run");campaign.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");game.LoadMap(campaign);game.LoginPlayer();
 auto* cp=game.viewport->Actor();check(cp,"campaign player");cp->DisableEvent("PlayerTick");check(RunePhysics::FindImpactLane(cp,origin,dir,wall),"campaign lane");origin-=dir*110.f;check(cp->SetLocation(origin),"campaign placement");cp->SetPhysics(PHYS_Walking);cp->GotoState("PlayerWalking","Begin");cp->SetRotation(Rotator::FromVector(dir));cp->ViewRotation()=cp->Rotation();cp->Health()=1000;
 auto* npc=UObject::TryCast<UPawn>(cp->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},origin+dir*80.f,Rotator::FromVector(-dir)));check(npc,"campaign NPC");
 npc->Health()=1000;npc->Enemy()=cp;npc->GotoState("Fighting","Begin");npc->SetPhysics(PHYS_Walking);npc->Velocity()=npc->Acceleration()=vec3(0);for(const char* event:{"Tick","Timer","SeePlayer","HearNoise"})npc->DisableEvent(event);
 auto* cs=give(cp,"VikingShortSword");check(RuneKick::Start(npc),"campaign NPC real kick");npc->AnimFrame()=.6f;cs->GotoState("Swinging","");auto nr=anatomy(npc);auto* nm=UObject::Cast<USkelModel>(GetRuneDisplayMesh(npc));auto nb=nm->ActorJointMatrices(npc);auto hit=Point(GetRuneMeshToWorld(npc)*nb[nr.knee[1]]);
 CallEvent(cs,"DoWeaponSwipe",{ExpressionValue::ObjectValue(npc),ExpressionValue::IntValue(1u<<nr.knee[1]),ExpressionValue::IntValue(0),ExpressionValue::VectorValue(hit),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(vec3(0))});
 check(Missing(npc)==1 && npc->Health()>0 && !RuneKick::Compatible(npc),"campaign NPC sever/hop eligibility");
 auto* ns=give(npc,"VikingShortSword");npc->SetObject("Weapon",ns);cp->RuneKickCooldown=0;cp->GotoState("PlayerWalking","Begin");cp->GetUObject("AnimProxy")->GotoState("Idle","Begin");cp->SetPhysics(PHYS_Walking);check(RuneKick::Start(cp),"campaign player kick");cp->AnimFrame()=.6f;ns->GotoState("Swinging","");auto cr=anatomy(cp);auto* cm=UObject::Cast<USkelModel>(GetRuneDisplayMesh(cp));hit=Point(GetRuneMeshToWorld(cp)*cm->ActorJointMatrices(cp)[cr.knee[1]]);
 CallEvent(ns,"DoWeaponSwipe",{ExpressionValue::ObjectValue(cp),ExpressionValue::IntValue(1u<<cr.knee[1]),ExpressionValue::IntValue(0),ExpressionValue::VectorValue(hit),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(vec3(0))});
 std::cout<<"KICK_COUNTER_CAMPAIGN_PLAYER health="<<cp->Health()<<" missing="<<Missing(cp)<<std::endl;check(Missing(cp)==1 && cp->Health()>0,"campaign player sever");std::cout<<"KICK_COUNTER_CAMPAIGN_OK NPC=1 player=1"<<std::endl;
 cp->Acceleration()=cp->Velocity()=vec3(0);game.SaveGameToSlot(87,"Kick counter injury fixture");game.LoadFromSaveFile(UnrealURL("?load=87"));game.PossessSavedPlayer();
 check(Missing(game.viewport->Actor())==1,"campaign injury not restored from save");unsigned savedNPC=0,savedLegs=0;for(auto* actor:game.Level->Actors){if(actor && actor->IsA("DarkViking") && Missing(actor)>=0)++savedNPC;if(actor && Detached(actor))++savedLegs;}
 check(savedNPC>0 && savedLegs>=2,"saved NPC/loose legs missing");std::cout<<"KICK_COUNTER_SAVE_OK player=1 NPC="<<savedNPC<<" loose_parts="<<savedLegs<<std::endl;
 // Original corpse InitFor must preserve the triangle mask, including models
 // whose legs share a material group with the rest of the body.
 UPawn* injured=nullptr;for(auto* actor:game.Level->Actors)if(actor && actor->IsA("DarkViking") && Missing(actor)>=0){injured=UObject::TryCast<UPawn>(actor);break;}
 check(injured,"corpse source");int injury=marker(injured);injured->Health()=0;
 auto* corpse=UObject::TryCast<UActor>(CallEvent(injured,"SpawnCarcass").ToObject());check(corpse,"injured corpse spawn");
 check(marker(corpse)==injury && TriangleMask(corpse) && *TriangleMask(corpse)==*TriangleMask(injured),"corpse regrew severed leg");
 std::cout<<"KICK_COUNTER_CORPSE_OK missing_leg_preserved=1"<<std::endl;
 std::cout<<"KICK_COUNTER_GEOMETRY_OK samples="<<samples<<std::endl;
}
