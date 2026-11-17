// Bounded real-package and BSP fixture, with isolated configuration/saves.
bool Fixture(UPlayerPawn* p,vec3& origin,vec3& normal,bool exitNeeded=false){
 auto spots=p->XLevel()->Actors;
 for(auto* spot:spots)if(spot && (spot->IsA("PlayerStart")||spot->IsA("PathNode")))for(int turn=0;turn<16;++turn){
  float yaw=turn*6.2831853f/16;vec3 direction(std::cos(yaw),std::sin(yaw),0);auto from=spot->Location();auto h=Trace(p,from,from+direction*400.f);
  if(!Surface(p,h)||h.Fraction*400<45)continue;auto hit=from+direction*(h.Fraction*400.f);vec3 at=hit-direction*(p->CollisionRadius()+3.f);at.z+=20;
  if(!p->CheckLocation(at,p->CollisionRadius(),p->CollisionHeight(),true).first || !p->SetLocation(at))continue;
  p->SetRotation(Rotator::FromVector(direction));p->ViewRotation()=p->Rotation();
  vec3 n;if(!Wall(p,at,direction,n))continue;
  if(exitNeeded){vec3 path[3];if(!ContinuousExit(p,at,n,path))continue;std::cout<<"SPIDER_EXIT_FIXTURE at="<<at.x<<","<<at.y<<","<<at.z<<" landing="<<path[2].x<<","<<path[2].y<<","<<path[2].z<<std::endl;}
  else if(!Clear(p,at,at+vec3(0,0,64)))continue;
  origin=at;normal=n;return true;
 }return false;
}
void Probe(Engine& game){
 auto check=[](bool b,const char* text){if(!b)throw std::runtime_error(std::string("Spider Monkeys: ")+text);};
 check(!(RuneBattleLaws::DefaultCampaign&RuneBattleLaws::Spider),"campaign default");RuneBattleLaws::Campaign=RuneBattleLaws::All;RuneTactics::Enabled=false;RunePhysics::RequestedMode=0;
 game.NetworkAuthority=true;auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.LoadMap(url);auto* p=game.LoginNetworkPlayer("Spider");check(p,"player");game.viewport->Actor()=p;
 p->DisableEvent("PlayerTick");p->GotoState("PlayerWalking","Begin");p->SetCollision(true,false,false);p->Health()=1000;
 unsigned rigs=0;for(const auto& c:openrune::Characters){auto* defaults=game.packages->FindClass(c.className)->GetDefaultObject<UPawn>();check(defaults,"roster class");auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(defaults));check(m,"roster model");unsigned clips=0;for(auto& seq:m->Sequences)for(auto name:{"ClimbUpRope","intropullupA","pullupTest"})if(seq.Info.Name==name)++clips;std::cout<<"SPIDER_RIG class="<<c.className<<" clips="<<clips<<std::endl;check(clips==3,"roster alternating climb and ledge animations");++rigs;}

 vec3 origin,normal;check(Fixture(p,origin,normal),"visible solid wall fixture");
 auto reset=[&]{Stop(p);p->Spider={};p->RuneKickActive=p->RuneKickPending=false;p->RuneBashPhase=0;p->Health()=1000;p->GotoState("PlayerWalking","Begin");if(auto* proxy=p->GetUObject("AnimProxy"))proxy->GotoState("Idle", "Begin");p->SetPhysics(PHYS_Falling);check(p->SetLocation(origin),"fixture placement");p->SetRotation(Rotator::FromVector(-normal));p->ViewRotation()=p->Rotation();p->Velocity()=p->Acceleration()=vec3(0);};
 reset();RuneBattleLaws::Campaign=RuneBattleLaws::DefaultCampaign;check(!Start(p),"campaign unchecked law");RuneBattleLaws::Campaign=RuneBattleLaws::All;
 p->SetPhysics(PHYS_Walking);check(!Start(p),"Use on floor changed pickup");p->SetPhysics(PHYS_Falling);CallEvent(p,"Use");check(Busy(p),"real Use did not grab");
 check(p->Physics()==PHYS_None,"wall gravity ownership");auto start=p->Location();Input(p,1,0);
 auto tick=[&](int n){for(int i=0;i<n;++i){game.TotalTime+=1.f/120;game.LevelInfo->TimeSeconds()+=1.f/120;p->DisableEvent("PlayerTick");game.Level->Tick(1.f/120,false);}};
 tick(45);std::cout<<"SPIDER_MOVE z="<<p->Location().z-start.z<<" active="<<p->Spider.active<<" axis="<<p->Spider.forward<<" age="<<p->Spider.age<<" stalled="<<p->Spider.stalled<<" physics="<<int(p->Physics())<<" state="<<p->GetStateName().ToString()<<std::endl;check(p->Location().z-start.z>20,"ordinary actor tick did not climb");check(p->AnimSequence()=="ClimbUpRope","wall traversal repeated two-hand pull-up");check(length((p->Location()-start).xy())<1,"wall penetration or side drift");
 auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));auto pose=m->SampleAnimation(p);for(auto& j:pose)check(std::isfinite(j.Position.x+j.Position.y+j.Position.z),"IK invalid pose");
 Input(p,0,1);start=p->Location();tick(12);check(length((p->Location()-start).xy())>3,"lateral climb");
 CallEvent(p,"Jump",{ExpressionValue::FloatValue(0)});check(!Busy(p)&&p->Physics()==PHYS_Falling&&dot(p->Velocity(),normal)>200&&p->Velocity().z>200,"jump away");
 reset();check(Start(p),"drop setup");CallEvent(p,"Use");check(!Busy(p)&&p->Physics()==PHYS_Falling,"Use drop");
 reset();check(Start(p),"law release setup");RuneBattleLaws::Campaign=RuneBattleLaws::DefaultCampaign;Tick(p,.02f);check(!Busy(p)&&p->Physics()==PHYS_Falling,"disabled law kept climber suspended");RuneBattleLaws::Campaign=RuneBattleLaws::All;
 reset();check(Start(p),"damage setup");auto* striker=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},origin+normal*60.f,Rotator::FromVector(-normal)));check(striker,"damage attacker");striker->Health()=1000;striker->Enemy()=p;
 check(RuneKick::Target(striker)==p,"wall climber immune to kick/bash targeting");
 CallEvent(p,"JointDamaged",{ExpressionValue::IntValue(10),ExpressionValue::ObjectValue(striker),ExpressionValue::VectorValue(p->Location()),ExpressionValue::VectorValue(normal*100.f),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
 check(p->Health()<1000 && !Busy(p)&&p->Physics()==PHYS_Falling,"committed hit did not release wall climber");striker->Destroy();p->Enemy()=nullptr;
 reset();check(Start(p),"save setup");BeforeSave();check(!Busy(p)&&p->Physics()==PHYS_Falling,"save persisted unsupported hang");
 // Check visible alternating support, not just whether a named animation plays.
 unsigned alternatingCases=0;
 for(const auto& c:openrune::Characters){
  auto* defaults=game.packages->FindClass(c.className)->GetDefaultObject<UPawn>();auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(defaults));size_t sequence=model->Sequences.size(),restSequence=0;
  for(size_t i=0;i<model->Sequences.size();++i){if(model->Sequences[i].Info.Name=="ClimbUpRope")sequence=i;if(model->Sequences[i].Info.Name=="neutral_idle")restSequence=i;}
  check(sequence<model->Sequences.size(),"missing alternating wall cycle");
  auto joint=[&](const char* name){for(size_t i=0;i<model->Joints.size();++i)if(model->Joints[i].Name==name)return int(i);return -1;};
  int leftHand=joint("lwrist"),rightHand=joint("rwrist"),leftFoot=joint("lankle"),rightFoot=joint("rankle"),pelvis=joint("pelvis"),neck=joint("neck");
  check(leftHand>=0&&rightHand>=0&&leftFoot>=0&&rightFoot>=0&&pelvis>=0&&neck>=0,"climb limb joints");
  auto rest=model->JointMatrices(model->DecodeFrame(restSequence,0));auto up=normalize(Point(rest[neck])-Point(rest[pelvis]));float handMin=1e9f,handMax=-1e9f,footMin=1e9f,footMax=-1e9f;
  for(int frame=0;frame<model->Sequences[sequence].Info.NumFrames;++frame){auto bones=model->JointMatrices(model->DecodeFrame(sequence,frame));float hands=dot(Point(bones[leftHand])-Point(bones[rightHand]),up),feet=dot(Point(bones[leftFoot])-Point(bones[rightFoot]),up);handMin=std::min(handMin,hands);handMax=std::max(handMax,hands);footMin=std::min(footMin,feet);footMax=std::max(footMax,feet);}
  check(handMin<-5 && handMax>5,"hands do not alternate reach");check(footMin<-2 && footMax>2,"feet do not alternate steps");++alternatingCases;
 }
 std::cout<<"RUNE_SPIDER_ALTERNATING_OK rigs="<<alternatingCases<<" hands=1 feet=1 pullup_only_at_top=1"<<std::endl;
 // Preserve original authored limb poses in both traversal and mantle clips.
 reset();check(Start(p),"original pose setup");p->bAnimLoop()=false;unsigned originalCases=0;
 for(const auto& character:openrune::Characters){auto* defaults=game.packages->FindClass(character.className)->GetDefaultObject<UPawn>();auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(defaults));
  for(size_t sequence=0;sequence<model->Sequences.size();++sequence)if(model->Sequences[sequence].Info.Name=="ClimbUpRope" || model->Sequences[sequence].Info.Name=="intropullupA" || model->Sequences[sequence].Info.Name=="pullupTest")for(int frame=0;frame<model->Sequences[sequence].Info.NumFrames;frame+=2){
   p->AnimSequence()=model->Sequences[sequence].Info.Name;p->AnimFrame()=float(frame)/std::max(1,model->Sequences[sequence].Info.NumFrames);p->AnimLast()=1.f-1.f/std::max(1,model->Sequences[sequence].Info.NumFrames);
   auto authored=model->DecodeFrame(sequence,frame),actual=authored;ApplyPose(*model,p,actual);
   for(size_t j=0;j<actual.size();++j){check(actual[j].Rotation==authored[j].Rotation && length(actual[j].Scale-authored[j].Scale)<1e-6f,"changed original climbing joint pose");if(j)check(length(actual[j].Position-authored[j].Position)<1e-6f,"changed original limb position");}
   ++originalCases;
  }
 }
 std::cout<<"RUNE_SPIDER_ORIGINAL_POSES_OK cases="<<originalCases<<" custom_limb_rotations=0"<<std::endl;Stop(p);
 std::cout<<"RUNE_SPIDER_CORE_OK roster="<<rigs<<" vertical=1 lateral=1 jump=1 drop=1 law=1 save=1"<<std::endl;
 // Find a real collision-clear top, then execute every mantle segment with TryMove.
 reset();bool ledge=Fixture(p,origin,normal,true);
 if(!ledge){auto next=game.GetDefaultURL("DM-Thorstadt.run");next.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.LoadMap(next);p=game.LoginNetworkPlayer("SpiderLedge");game.viewport->Actor()=p;p->Health()=1000;p->SetCollision(true,false,false);p->GotoState("PlayerWalking","Begin");ledge=Fixture(p,origin,normal,true);}
 check(ledge,"real ledge exit fixture");reset();check(Start(p),"ledge grab");Input(p,1,0);float z=p->Location().z;bool mantle=false;
 for(int i=0;i<900 && Busy(p);++i){tick(1);mantle=mantle||p->Spider.mantle;if(Busy(p)&&p->Spider.mantle)check(p->AnimSequence()=="intropullupA" || p->AnimSequence()=="pullupTest","mantle missing ledge finish");}
 std::cout<<"SPIDER_MANTLE_RESULT started="<<mantle<<" busy="<<Busy(p)<<" rise="<<p->Location().z-z<<" physics="<<int(p->Physics())<<" state="<<p->GetStateName().ToString()<<std::endl;check(mantle&&!Busy(p)&&p->Location().z>z+8,"clear ledge mantle did not finish");check(p->CheckLocation(p->Location(),p->CollisionRadius(),p->CollisionHeight(),true).first,"mantle penetrated world");
 std::cout<<"RUNE_SPIDER_MANTLE_OK rise="<<p->Location().z-z<<std::endl;
 #ifdef OPENRUNE_ENABLE_NETWORK
 {reset();RuneNetworkAuthority authority(game);check(authority.bind(1,p,1),"network bind");authority.beginTick(1,1000);p->SetPhysics(PHYS_Walking);
 openrune::net::AdmittedAction jump;jump.action.id=1;jump.action.life=1;jump.action.kind=openrune::net::ActionKind::Jump;jump.localTime=1000;
 check(authority.action(1,jump,1000),"network jump admission");
 p->EnableEvent("PlayerTick");for(int frame=0;frame<8;++frame){game.TotalTime+=1.f/120;game.LevelInfo->TimeSeconds()+=1.f/120;game.Level->Tick(1.f/120,false);}
 check(p->Physics()==PHYS_Falling,"ordinary network jump");
 openrune::net::AdmittedAction use=jump;use.action.id=2;use.action.kind=openrune::net::ActionKind::Use;use.localTime=1100;
 authority.beginTick(2,1100);check(authority.action(1,use,1100)&&Busy(p),"ordinary network Use grab");openrune::net::Input input;input.life=1;input.sequence=1;input.forward=1000;input.yaw=uint16_t(p->Rotation().Yaw);check(authority.input(1,input),"network input");
 check(p->Spider.forward==1,"network wall axes");openrune::net::World world;check(authority.capture(world),"network capture");RuneNetworkReplica replica;
 for(auto& event:authority.pendingActors()){if(event.operation==openrune::net::ActorOperation::Spawn)check(replica.spawn(game,event),"replica spawn");else if(event.operation==openrune::net::ActorOperation::Appearance)check(replica.appearance(game,event),"replica appearance");}
 openrune::net::Snapshot snap{1,0,1,world},decoded;openrune::net::Bytes encoded;check(openrune::net::encodeSnapshot(snap,nullptr,encoded)&&openrune::net::decodeSnapshot(encoded,nullptr,decoded),"serialized climb snapshot");
 check(replica.install(decoded),"replica snapshot");check(Busy(replica.actor(authority.identity(p))),"remote climb flag");check(!authority.movementAck(1),"walking prediction active during climb");openrune::net::MatchStatus match;match.phase=openrune::net::MatchPhase::Playing;match.mode=openrune::net::GameMode::Deathmatch;
 RuneBot::Brain bot(99);auto decision=bot.think(game,replica,world,match,authority.identity(p),2000);check(decision.input.forward==1000,"bot does not climb when attached");
 RuneBattleLaws::Campaign=RuneBattleLaws::DefaultCampaign;decision=bot.think(game,replica,world,match,authority.identity(p),2200);check(decision.action==openrune::net::ActionKind::Use,"bot ignores disabled climb law");RuneBattleLaws::Campaign=RuneBattleLaws::All;
 authority.unbind(1);check(!Busy(p),"disconnect left climber suspended");
 std::cout<<"RUNE_SPIDER_NETWORK_OK axes=1 snapshot=1 prediction_suspended=1 disconnect=1 bot_input=1 bot_law=1"<<std::endl;}
 #endif
 // A real campaign NPC plans the high exit and traverses it using shared collision.
 game.NetworkAuthority=false;auto campaign=game.GetDefaultURL("DM-Hildir.run");campaign.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");game.LoadMap(campaign);game.LoginPlayer();p=game.viewport->Actor();p->Health()=1000;p->GotoState("PlayerWalking","Begin");p->DisableEvent("PlayerTick");
 check(Fixture(p,origin,normal,true),"campaign climb route");vec3 path[3];check(ContinuousExit(p,origin,normal,path),"campaign continuous wall");check(p->SetLocation(path[2]-normal*70.f),"campaign goal position");p->SetPhysics(PHYS_Walking);
 auto* npc=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},origin,Rotator::FromVector(-normal)));check(npc,"campaign NPC spawn");npc->Health()=1000;npc->Enemy()=p;npc->GotoState("Fighting","");npc->SetPhysics(PHYS_Walking);npc->Velocity()=npc->Acceleration()=vec3(0);npc->Spider.think=0;
 for(auto event:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible"})npc->DisableEvent(event);npc->StateFrame->SetLatentState(LatentRunState::Stop);
 check(npc->SetLocation(origin+normal*(npc->CollisionRadius()-p->CollisionRadius()+2.f)),"NPC wall approach placement");
 vec3 planNormal,planExit;std::cout<<"SPIDER_NPC_INPUT eligible="<<Eligible(npc)<<" r="<<npc->CollisionRadius()<<" h="<<npc->CollisionHeight()<<" at="<<npc->Location().x<<","<<npc->Location().y<<","<<npc->Location().z<<" goal="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" state="<<npc->GetStateName().ToString()<<std::endl;check(Plan(npc,p->Location(),planNormal,planExit),"NPC high-goal planner");float began=npc->Location().z;bool climbed=false,topped=false;
 for(int frame=0;frame<1080;++frame){tick(1);climbed=climbed||Busy(npc);topped=topped||npc->Spider.mantle;if(climbed&&!Busy(npc))break;}
 check(climbed&&topped&&!Busy(npc)&&npc->Location().z>began+40,"campaign NPC did not finish planned climb");
 std::cout<<"RUNE_SPIDER_CAMPAIGN_AI_OK rise="<<npc->Location().z-began<<" climbed=1 mantled=1"<<std::endl;
 std::cout<<"RUNE_SPIDER_OK"<<std::endl;
}
void Demo(Engine& game,int frame){
 if(!commandline->HasArg("","--spider-demo"))return;
 if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Spider demo requires isolated saves");
 static vec3 at,n,camera,look;static bool ready=false;auto* p=game.viewport->Actor();
 if(frame==90){
  RuneBattleLaws::Campaign=RuneBattleLaws::All;RuneTactics::Enabled=false;p->Health()=1000;p->GotoState("PlayerWalking","Begin");
  if(!Fixture(p,at,n,commandline->HasArg("","--spider-mantle-demo")))throw std::runtime_error("Spider demo wall");p->SetPhysics(PHYS_Falling);p->Velocity()=vec3(0);p->Spider={};if(!Start(p))throw std::runtime_error("Spider demo grab");
  auto side=cross(vec3(0,0,1),-n);look=at+vec3(0,0,30);camera=at+n*170.f+side*100.f+vec3(0,0,45);
  if(!p->FastTrace(camera,look))camera=at+n*150.f-side*85.f+vec3(0,0,25);
  if(commandline->HasArg("","--spider-mantle-demo")){auto side=cross(vec3(0,0,1),-n);look=at+vec3(0,0,120);camera=at+n*300.f+side*180.f+vec3(0,0,180);}
  ready=true;LogMessage("RUNE_SPIDER_DEMO_READY");
 }
 if(!ready)return;p->DisableEvent("PlayerTick");
 if(commandline->HasArg("","--spider-mantle-demo"))Input(p,1,0);else if(frame<170)Input(p,1,0);else if(frame<205)Input(p,0,1);else Input(p,0,0);
 if(frame==245 && !commandline->HasArg("","--spider-mantle-demo"))CallEvent(p,"Jump",{ExpressionValue::FloatValue(0)});
 if(commandline->HasArg("","--spider-mantle-demo")){
  auto side=cross(vec3(0,0,1),-n);look=p->Location()+vec3(0,0,8);auto ideal=look+n*135.f+side*70.f+vec3(0,0,25);auto obstruction=Trace(p,look,ideal);camera=look+(ideal-look)*std::max(.1f,obstruction.Fraction-.03f);
 }
 game.CameraLocation=camera;game.CameraRotation=Rotator::FromVector(look-camera);
 if(commandline->HasArg("","--spider-captures"))for(int capture:{110,140,170,200,235,260,300,360,400,450,500})if(frame==capture){
  LogMessage("RUNE_SPIDER_DEMO_FRAME frame="+std::to_string(frame)+" climb="+std::to_string(Busy(p))+" rise="+std::to_string(p->Location().z-at.z));
  auto* device=game.window->GetRenderDevice();int w=device->GetRenderWidth(),h=device->GetRenderHeight();Array<TextureColor> pixels;pixels.resize(size_t(w)*h);bool gamma=device->GammaCorrectScreenshots;device->GammaCorrectScreenshots=true;device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=gamma;
  std::ofstream f(commandline->GetArg("","--spider-captures","")+"-"+std::to_string(frame)+".ppm",std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";for(auto c:pixels){f.put(char(c.R));f.put(char(c.G));f.put(char(c.B));}
 }
}
