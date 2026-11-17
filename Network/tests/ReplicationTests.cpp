#include "openrune/net/Replication.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <fstream>
using namespace openrune::net;
namespace
{
size_t checks=0;
void require(bool v,const char* e,int line) { ++checks; if (!v) throw std::runtime_error(std::to_string(line)+": "+e); }
#define CHECK(...) require(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
auto identity=[](uint64_t t)->std::optional<uint64_t>{return t;};
MatchContract testMatch()
{
    MatchContract m;m.map="DM-Hildir";m.gameplayHash.fill(0x31);m.mapHash.fill(0x72);return m;
}
World makeWorld(size_t n)
{
    World w;
    for (size_t i=0;i<n;++i)
    {
        Entity e; e.ref={uint32_t(i+1),1}; e.position={int32_t(i*21),-100,300};
        e.health=100; e.flags=3; e.animationRate=256; w.push_back(e);
    }
    if (n>=3) { w[1].kind=EntityKind::Weapon; w[1].owner=w[0].ref; w[1].attachmentJoint=12; w[0].weapon=w[1].ref; w[2].kind=EntityKind::Shield; w[0].shield=w[2].ref; w[2].owner=w[0].ref; }
    return w;
}
std::vector<SnapshotPart> parts(Snapshot s,const Snapshot* base=nullptr)
{
    Bytes b; CHECK(encodeSnapshot(s,base,b)); std::vector<SnapshotPart> p;
    size_t count=(b.size()+SnapshotPartBytes-1)/SnapshotPartBytes;
    for (size_t i=0;i<count;++i) p.push_back({s.id,s.baseline,s.tick,uint32_t(b.size()),uint8_t(i),uint8_t(count),Bytes(b.begin()+i*SnapshotPartBytes,b.begin()+std::min(b.size(),(i+1)*SnapshotPartBytes))});
    return p;
}
void codecs()
{
    for(uint8_t character=0;character<openrune::Characters.size();++character)
        for(uint8_t skin=0;skin<openrune::Characters[character].skins;++skin) {
            openrune::CharacterChoice choice{character,skin},decoded{255,255};Bytes bytes;
            CHECK(encodeCharacterChoice(7,choice,bytes));CHECK(bytes.size()==11);
            CHECK(decodeCharacterChoice(7,bytes,decoded) && decoded==choice);
            for(size_t n=0;n<bytes.size();++n)CHECK(!decodeCharacterChoice(7,std::span(bytes).first(n),decoded) && decoded==choice);
            CHECK(!decodeCharacterChoice(8,bytes,decoded));
            auto bad=bytes;bad[9]=36;CHECK(!decodeCharacterChoice(7,bad,decoded));
            bad=bytes;bad[10]=2;CHECK(!decodeCharacterChoice(7,bad,decoded));
            bytes.push_back(0);CHECK(!decodeCharacterChoice(7,bytes,decoded));
        }
    Bytes choiceBytes;CHECK(!encodeCharacterChoice(0,{},choiceBytes));
    CHECK(!encodeCharacterChoice(7,{36,0},choiceBytes));CHECK(!encodeCharacterChoice(7,{0,1},choiceBytes));
    auto w=makeWorld(512); CHECK(validWorld(w));
    Snapshot s{1,0,100,w}, decoded;
    Bytes b; CHECK(encodeSnapshot(s,nullptr,b)); CHECK(b.size()==39455);
    CHECK(decodeSnapshot(b,nullptr,decoded)); CHECK(s==decoded);
    for (size_t n=0;n<b.size();++n) { auto keep=decoded; CHECK(!decodeSnapshot(std::span(b).first(n),nullptr,decoded)); CHECK(keep==decoded); }
    // Native extension remains bounded and fits the existing 64-piece transfer.
    auto native=s;
    for(size_t i=10;i<74;++i)native.world[i].combat={ {},4,0,100,0,0,0,3 };
    CHECK(encodeSnapshot(native,nullptr,b));CHECK(b.size()==39455+64*19);
    CHECK(parts(native).size()==64);CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==native);
    auto overflow=native;overflow.world[74].combat.meter=1;CHECK(!validWorld(overflow.world));
    auto dual=s;dual.world[2].kind=EntityKind::Weapon;dual.world[0].shield={};dual.world[2].attachmentJoint=13;
    dual.world[0].combat={dual.world[2].ref,3,255,0,0,0,0,0};
    CHECK(encodeSnapshot(dual,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==dual);
    auto powered=dual;powered.world[0].combat.runeCapacity=100;powered.world[0].combat.runePower=75;
    powered.world[1].flags|=128;
    CHECK(encodeSnapshot(powered,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==powered);
    auto invalidPower=powered;invalidPower.world[0].combat.runePower=101;CHECK(!validWorld(invalidPower.world));
    auto invalidDual=dual;invalidDual.world[0].combat.offhand=dual.world[0].weapon;CHECK(!validWorld(invalidDual.world));
    invalidDual=dual;invalidDual.world[2].owner={};CHECK(!validWorld(invalidDual.world));
    invalidDual=dual;invalidDual.world[2].attachmentJoint=255;CHECK(!validWorld(invalidDual.world));
    invalidDual=dual;invalidDual.world[0].combat.flags=16;CHECK(!validWorld(invalidDual.world));
    invalidDual=dual;invalidDual.world[0].combat.meter=101;CHECK(!validWorld(invalidDual.world));
    // Server-owned readiness may be granted by the optional normal-combat rule.
    auto normal=dual;normal.world[0].combat.ready=1;
    CHECK(encodeSnapshot(normal,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==normal);
    invalidDual=normal;invalidDual.world[0].combat.cooldown=1;CHECK(!validWorld(invalidDual.world));
    invalidDual=normal;invalidDual.world[0].combat.phase=1;CHECK(!validWorld(invalidDual.world));
    auto slam=normal;slam.world[0].combat={};slam.world[0].combat.phase=6;slam.world[0].combat.age=225;
    CHECK(encodeSnapshot(slam,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==slam);
    slam.world[0].combat.phase=7;CHECK(encodeSnapshot(slam,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded) && decoded==slam);
    slam.world[0].combat.phase=8;CHECK(!validWorld(slam.world));
    auto endDual=dual;endDual.id=2;endDual.baseline=1;endDual.tick++;endDual.world[0].combat={};
    CHECK(encodeSnapshot(endDual,&dual,b));CHECK(decodeSnapshot(b,&dual,decoded) && decoded==endDual);
    auto physical=s;physical.world[0].physical.kind=1;physical.world[0].physical.root={-25,120,13};physical.world[0].physical.joints.resize(38,{65000,127,32900});
    CHECK(encodeSnapshot(physical,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded)&&decoded==physical);
    auto physicalNext=physical;physicalNext.id=2;physicalNext.baseline=1;physicalNext.tick++;physicalNext.world[0].physical.joints[1][0]++;
    CHECK(encodeSnapshot(physicalNext,&physical,b));CHECK(decodeSnapshot(b,&physical,decoded)&&decoded==physicalNext);
    for(size_t n=0;n<b.size();++n)CHECK(!decodeSnapshot(std::span(b).first(n),&physical,decoded)&&decoded==physicalNext);
    auto seeded=physical;seeded.world[0].physical.kind=3;
    seeded.world[0].physical.basis={{1,{-32704,24,32704},{4096,2048,16384}},{37,{2,3,4},{1,4096,4096}}};
    CHECK(encodeSnapshot(seeded,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded)&&decoded==seeded);
    for(size_t n=0;n<b.size();++n)CHECK(!decodeSnapshot(std::span(b).first(n),nullptr,decoded)&&decoded==seeded);
    auto movedSeed=seeded;movedSeed.id=2;movedSeed.baseline=1;movedSeed.tick++;
    movedSeed.world[0].physical.joints[0][0]++;
    CHECK(encodeSnapshot(movedSeed,&seeded,b));CHECK(decodeSnapshot(b,&seeded,decoded)&&decoded==movedSeed);
    auto badSeed=seeded;badSeed.world[0].physical.basis.clear();CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.basis[1].joint=1;CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.basis[1].joint=38;CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.basis[0].scale[1]=0;CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.basis[0].scale[1]=16385;CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.kind=1;CHECK(!validWorld(badSeed.world));
    badSeed=seeded;badSeed.world[0].physical.kind=4;CHECK(!validWorld(badSeed.world));
    auto invalidPhysical=physical;invalidPhysical.world[0].physical.joints.resize(65);CHECK(!validWorld(invalidPhysical.world));
    invalidPhysical=physical;invalidPhysical.world[0].physical.kind=0;CHECK(!validWorld(invalidPhysical.world));
    invalidPhysical=physical;invalidPhysical.world[1].physical=physical.world[0].physical;CHECK(!validWorld(invalidPhysical.world));
    auto rigid=s;rigid.world[0].physical.kind=2;rigid.world[0].physical.rotation={0,0,0,32767};CHECK(encodeSnapshot(rigid,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded)&&decoded==rigid);
    rigid.world[0].physical.rotation={};CHECK(!validWorld(rigid.world));
    auto spider=s;spider.world[0].flags=1;spider.world[0].combat.flags=16;CHECK(encodeSnapshot(spider,nullptr,b));CHECK(decodeSnapshot(b,nullptr,decoded)&&decoded==spider);
    spider.world[0].flags=3;CHECK(!validWorld(spider.world)); // A wall climber is not grounded.
    spider.world[0].flags=0;CHECK(!validWorld(spider.world)); // A dead/unknown life cannot climb.
    auto bad=w; bad.push_back(w.back()); CHECK(!validWorld(bad));
    bad=w; bad[1].ref=bad[0].ref; CHECK(!validWorld(bad));
    bad=w; bad[0].shield.generation=2; CHECK(!validWorld(bad));
    bad=w; bad[0].shield=bad[1].ref; CHECK(!validWorld(bad));
    bad=w; bad[0].owner=bad[1].ref; CHECK(!validWorld(bad));
    bad=w; bad[10].position[0]=INT32_MAX; CHECK(!validWorld(bad));
    bad=w; bad[10].flags=64; CHECK(!validWorld(bad));
    bad=w; bad[10].attachmentJoint=4; CHECK(!validWorld(bad)); // Attachment must have an owner.
    Snapshot delta{2,1,101,w}; delta.world[0].position[0]+=17; delta.world.back().ref.generation=2;
    CHECK(encodeSnapshot(delta,&s,b)); CHECK(b.size()==185); CHECK(decodeSnapshot(b,&s,decoded)); CHECK(decoded==delta);
    CHECK(!decodeSnapshot(b,nullptr,decoded)); auto wrong=s; wrong.id=9; CHECK(!decodeSnapshot(b,&wrong,decoded));
    wrong.id=0; CHECK(!encodeSnapshot({2,0,101,w},&wrong,b));
    auto invalid=delta; invalid.world[1].ref.generation=0; CHECK(!encodeSnapshot(invalid,&s,b));
    invalid=delta; invalid.world[10].kind=EntityKind::Mover; CHECK(!encodeSnapshot(invalid,&s,b));
    invalid=delta; invalid.world[10].life=0; CHECK(!encodeSnapshot(invalid,&s,b));
    invalid=delta; invalid.world[10].life=2; CHECK(encodeSnapshot(invalid,&s,b));
    auto regressed=invalid; regressed.id=3; regressed.baseline=2; regressed.world[10].life=1;
    CHECK(!encodeSnapshot(regressed,&invalid,b));
    delta.world.erase(delta.world.end()-5,delta.world.end()); CHECK(encodeSnapshot(delta,&s,b)); CHECK(decodeSnapshot(b,&s,decoded)); CHECK(decoded==delta);
    auto extra=b; extra.push_back(1); CHECK(!decodeSnapshot(extra,&s,decoded));
    delta.world=makeWorld(1); CHECK(encodeSnapshot(delta,&s,b)); CHECK(decodeSnapshot(b,&s,decoded)); CHECK(decoded==delta);
    Snapshot empty{3,2,102,{}}; CHECK(encodeSnapshot(empty,&delta,b)); CHECK(decodeSnapshot(b,&delta,decoded)); CHECK(decoded==empty);

    InputBatch ib; ib.inputs={{1,123,1,-1000,1000,-16384,65535,7}}; ib.actions={{1,1,123,1,ActionKind::Kick}};
    ib.snapshots={4,5,3,false}; InputBatch ir;
    CHECK(encodeInputs(7,ib,b)); CHECK(decodeInputs(7,b,ir)); CHECK(ir.inputs==ib.inputs && ir.actions==ib.actions);
    auto keep=ir; for (size_t i=0;i<b.size();++i) { CHECK(!decodeInputs(7,std::span(b).first(i),ir)); CHECK(ir.inputs==keep.inputs); }
    CHECK(!decodeInputs(8,b,ir)); ib.inputs[0].held=8; CHECK(!encodeInputs(7,ib,b)); ib.inputs[0].held=0;
    ib.inputs.push_back(ib.inputs[0]); CHECK(!encodeInputs(7,ib,b)); ib.inputs.pop_back();
    ib.actions[0].kind=ActionKind(255); CHECK(!encodeInputs(7,ib,b));
    InputFeedback f{123,{{1,Admission::Admitted},{2,Admission::Expired}}},fr;
    CHECK(encodeFeedback(7,f,b)); CHECK(decodeFeedback(7,b,fr)); CHECK(fr.actions==f.actions);
    f.actions[1].id=1; CHECK(!encodeFeedback(7,f,b));
    for (auto p:parts(s))
    {
        SnapshotPart pr; CHECK(encodePart(7,p,b)); CHECK(b.size()<=MaxPayloadBytes); CHECK(decodePart(7,b,pr)); CHECK(pr.bytes==p.bytes);
        for (size_t i=0;i<b.size();++i) CHECK(!decodePart(7,std::span(b).first(i),pr));
        CHECK(!decodePart(8,b,pr)); p.count=64; CHECK(!encodePart(7,p,b));
    }
    CombatOutcome kick{1,120,4,{1,1},{2,1},1,1,CombatMove::Kick,CombatContact::Blocked,90,false}, result;
    CHECK(encodeCombatOutcome(7,kick,b) && b.size()==62);
    CHECK(decodeCombatOutcome(7,b,result) && result==kick);
    for (size_t i=0;i<b.size();++i) { auto unchanged=result; CHECK(!decodeCombatOutcome(7,std::span(b).first(i),result) && result==unchanged); }
    CHECK(!decodeCombatOutcome(8,b,result)); auto extraCombat=b; extraCombat.push_back(0); CHECK(!decodeCombatOutcome(7,extraCombat,result));
    auto invalidCombat=kick; invalidCombat.target={}; CHECK(!encodeCombatOutcome(7,invalidCombat,b));
    invalidCombat=kick; invalidCombat.staggered=true; CHECK(!encodeCombatOutcome(7,invalidCombat,b));
    invalidCombat=kick; invalidCombat.contact=CombatContact::Miss; CHECK(!encodeCombatOutcome(7,invalidCombat,b));
    invalidCombat=kick; invalidCombat.move=CombatMove(4); CHECK(!encodeCombatOutcome(7,invalidCombat,b));
    invalidCombat=kick; invalidCombat.id=0; CHECK(!encodeCombatOutcome(7,invalidCombat,b));
    auto miss=kick; miss.contact=CombatContact::Miss; miss.target={}; miss.targetLife=0; miss.targetHealth=0;
    CHECK(encodeCombatOutcome(7,miss,b) && decodeCombatOutcome(7,b,result) && result==miss);
    auto match=testMatch();MatchContract decodedMatch;
    CHECK(encodeMatchContract(7,match,b) && decodeMatchContract(7,b,decodedMatch) && decodedMatch==match);
    for(size_t i=0;i<b.size();++i)CHECK(!decodeMatchContract(7,std::span(b).first(i),decodedMatch));
    CHECK(!decodeMatchContract(8,b,decodedMatch));
    match.mapHash[0]^=1;CHECK(match!=decodedMatch);
    f=InputFeedback{123,{{1,Admission::Admitted}}};
    SoundCue sound;sound.id=1;sound.source={1,1};sound.emitter=2;sound.object=3;sound.package="RuneSFX";sound.position={10,-20,30};
    std::vector<SoundCue> decodedSounds;InputFeedback soundFeedback;
    CHECK(encodeSoundFeedback(7,f,{sound},b));auto soundBytes=b;
    CHECK(decodeSoundFeedback(7,b,soundFeedback,decodedSounds) && decodedSounds==std::vector<SoundCue>{sound} && soundFeedback.highestInput==f.highestInput);
    for(size_t n=0;n<soundBytes.size();++n)CHECK(!decodeSoundFeedback(7,std::span(soundBytes).first(n),soundFeedback,decodedSounds));
    CHECK(!decodeSoundFeedback(8,soundBytes,soundFeedback,decodedSounds));
    sound.package="../bad";CHECK(!encodeSoundFeedback(7,f,{sound},b));sound.package="RuneSFX";
    sound.pitch=0;CHECK(!encodeSoundFeedback(7,f,{sound},b));sound.pitch=256;
    CHECK(!encodeSoundFeedback(7,f,{sound,sound},b));
    sound.id=2;CHECK(encodeSoundFeedback(7,f,{sound},b));
    ActorEvent spawn{1,{5,2},ActorOperation::Spawn,EntityKind::Pawn,"RuneI","Ragnar","RuneI","RunePlayerProxy"},decodedActor;
    CHECK(encodeActorEvent(7,spawn,b) && decodeActorEvent(7,b,decodedActor) && decodedActor==spawn);
    for(size_t i=0;i<b.size();++i)CHECK(!decodeActorEvent(7,std::span(b).first(i),decodedActor));
    auto despawn=spawn;despawn.id=2;despawn.operation=ActorOperation::Despawn;
    despawn.package.clear();despawn.actorClass.clear();despawn.proxyPackage.clear();despawn.proxyClass.clear();
    CHECK(encodeActorEvent(7,despawn,b) && decodeActorEvent(7,b,decodedActor) && decodedActor==despawn);
    ActorEvent mover{3,{8,1},ActorOperation::Spawn,EntityKind::Mover,"Engine","Mover","","","Mover7"}, decodedMover;
    CHECK(encodeActorEvent(7,mover,b) && decodeActorEvent(7,b,decodedMover) && decodedMover==mover);
    mover.mapActor.clear();CHECK(!encodeActorEvent(7,mover,b));
    mover.mapActor="../Mover7";CHECK(!encodeActorEvent(7,mover,b));
    mover.mapActor="Mover7";mover.kind=EntityKind::Pawn;CHECK(!encodeActorEvent(7,mover,b));
    ActorEvent effect{4,{9,1},ActorOperation::Spawn,EntityKind::Effect,"RuneI","BloodSpray"};
    CHECK(encodeActorEvent(7,effect,b) && decodeActorEvent(7,b,decodedActor) && decodedActor==effect);
    effect.kind=EntityKind(uint8_t(EntityKind::Effect)+1);CHECK(!encodeActorEvent(7,effect,b));
    spawn.actorClass="../../bad";CHECK(!encodeActorEvent(7,spawn,b));
    ActorEvent visual;visual.id=4;visual.ref={5,2};visual.operation=ActorOperation::Appearance;
    visual.appearance.mesh=2;visual.appearance.firstGroup=8;visual.appearance.groupFlags[3]=2;visual.appearance.jointFlags[7]=1;
    visual.appearance.skins[7]={"RuneFX",42};
    visual.appearance.skeleton={"Players",12};visual.appearance.substitute={"HallsOfValhalla",2};visual.appearance.scale=-424;visual.kind=EntityKind::Debris;
    CHECK(encodeActorEvent(7,visual,b) && decodeActorEvent(7,b,decodedActor) && decodedActor==visual);
    for(size_t i=0;i<b.size();++i){auto old=decodedActor;CHECK(!decodeActorEvent(7,std::span(b).first(i),decodedActor) && decodedActor==old);}
    auto malformed=visual;malformed.appearance.firstGroup=9;CHECK(!encodeActorEvent(7,malformed,b));
    malformed=visual;malformed.appearance.skins[7].package="../RuneFX";CHECK(!encodeActorEvent(7,malformed,b));
    malformed=visual;malformed.appearance.skins[7].object=0;CHECK(!encodeActorEvent(7,malformed,b));
    auto weapon=kick;weapon.move=CombatMove::Weapon;CHECK(encodeCombatOutcome(7,weapon,b) && decodeCombatOutcome(7,b,result) && result==weapon);
    ActionResult decision{17,Admission::ExecutionRejected},decodedDecision;
    CHECK(encodeActionDecision(7,decision,b) && decodeActionDecision(7,b,decodedDecision) && decodedDecision==decision);
    for(size_t i=0;i<b.size();++i)CHECK(!decodeActionDecision(7,std::span(b).first(i),decodedDecision));
    decision.status=Admission::Admitted;CHECK(!encodeActionDecision(7,decision,b));
}
void inputActions()
{
    InputSender sender; InputReceiver receiver;
    Input in; in.life=1; in.forward=1000; in.held=2;
    CHECK(sender.capture(in,100)==1); CHECK(sender.act(ActionKind::Kick,101)==1);
    auto batch=sender.batch(102); CHECK(batch.inputs.size()==1 && batch.actions.size()==1);
    CHECK(receiver.receive(batch,120,identity)); CHECK(receiver.held(120).held==2);
    auto actions=receiver.drainActions(120); CHECK(actions.size()==1 && actions[0].action.kind==ActionKind::Kick);
    CHECK(receiver.receive(batch,121,identity)); CHECK(receiver.drainActions(121).empty());
    sender.receive(receiver.feedback()); CHECK(sender.pending()==0); CHECK(sender.drainResults()==std::vector<ActionResult>{{1,Admission::Admitted}});
    CHECK(receiver.resolve(1,Admission::Started));
    sender.receive(receiver.feedback()); CHECK(sender.drainResults()==std::vector<ActionResult>{{1,Admission::Started}});
    sender.receive(receiver.feedback()); CHECK(sender.drainResults().empty());
    CHECK(receiver.resolve(1,Admission::Cancelled));
    sender.receive(receiver.feedback()); CHECK(sender.drainResults()==std::vector<ActionResult>{{1,Admission::Cancelled}});
    CHECK(!receiver.resolve(1,Admission::Started));
    CHECK(receiver.held(351).held==0); // Lost release cannot leave a shield held forever.
    in.held=0; CHECK(sender.capture(in,360)); auto release=sender.batch(360); CHECK(receiver.receive(release,370,identity)); CHECK(receiver.held(370).held==0);
    CHECK(receiver.receive(batch,380,identity)); CHECK(receiver.held(380).sequence==2);
    auto conflict=release; conflict.inputs.back().forward=5; CHECK(!receiver.receive(conflict,381,identity)); CHECK(receiver.held(381).forward==1000);
    CHECK(sender.act(ActionKind::Use,382)); batch=sender.batch(383); CHECK(receiver.receive(batch,700,identity)); CHECK(receiver.drainActions(700).empty()); CHECK(receiver.feedback().actions.back().status==Admission::Expired);
    CHECK(!receiver.receive(batch,699,identity));
    CHECK(sender.capture(in,710)); CHECK(sender.act(ActionKind::Throw,711)); receiver.setLife(2);
    CHECK(receiver.receive(sender.batch(712),720,identity)); CHECK(receiver.feedback().actions.back().status==Admission::WrongLife); CHECK(receiver.held(720).held==0);
    InputSender missing; CHECK(missing.capture(in,800)); CHECK(missing.act(ActionKind::Attack,800)); auto m=missing.batch(800); m.inputs.clear();
    InputReceiver r; CHECK(r.receive(m,800,identity)); CHECK(r.feedback().actions[0].status==Admission::MissingInput);
    CHECK(r.receive(missing.batch(801),801,identity)); CHECK(r.drainActions(801).empty()); // Terminal rejection can't turn into a later hit.
    InputSender timeout; CHECK(timeout.capture(in,1000)); CHECK(timeout.act(ActionKind::Bash,1000)); CHECK(timeout.batch(1251).actions.empty());
    timeout.batch(2000); CHECK(timeout.drainUnconfirmed()==std::vector<uint64_t>{1}); CHECK(timeout.pending()==0);
    timeout.receive({1,{{1,Admission::Admitted}}}); CHECK(timeout.drainResults().size()==1); timeout.receive({1,{{1,Admission::Admitted}}}); CHECK(timeout.drainResults().empty());
    InputReceiver flood;
    for (uint64_t id=1;id<=300;++id)
    {
        InputBatch a; a.inputs={{id,1000,1,0,0,0,0,0}}; a.actions={{id,id,1000,1,ActionKind::Attack}};
        CHECK(flood.receive(a,1000,identity));
    }
    CHECK(flood.drainActions(1000).size()==8); CHECK(flood.feedback().actions.back().status==Admission::RateLimited);
    CHECK(flood.receive({{{1,1000,1,0,0,0,0,0}},{{1,1,1000,1,ActionKind::Attack}},{}},1000,identity));
    auto f=flood.feedback(); CHECK(f.actions.front().status==Admission::TooOld); CHECK(flood.drainActions(1000).empty());
    CHECK(!flood.receive({{},{{1000,1,1000,1,ActionKind::Attack}},{}},1000,identity));
    InputReceiver busy;
    for (uint64_t id=1;id<=65;++id)
        CHECK(busy.receive({{{id,id*50,1,0,0,0,0,0}},{{id,id,id*50,1,ActionKind::Attack}},{}},id*50,identity));
    CHECK(busy.feedback().actions.back().status==Admission::Admitted); CHECK(busy.drainActions(3250).size()==6);
    InputReceiver delayed;
    CHECK(delayed.receive({{{1,1000,1,0,0,0,0,0}},{{1,1,1000,1,ActionKind::Use}},{}},1000,identity));
    CHECK(delayed.drainActions(1251).empty()); // Adapter stalls must not revive an old action.
    CHECK(delayed.feedback().actions.back().status==Admission::Expired);
    InputSender rejected; InputReceiver rejectedHost;
    CHECK(rejected.capture(in,1500)); CHECK(rejected.act(ActionKind::Bash,1500));
    CHECK(rejectedHost.receive(rejected.batch(1500),1500,identity));
    CHECK(rejectedHost.drainActions(1500).size()==1);
    CHECK(rejectedHost.resolve(1,Admission::ExecutionRejected));
    rejected.receive(rejectedHost.feedback());
    CHECK(rejected.drainResults()==std::vector<ActionResult>{{1,Admission::ExecutionRejected}});
    // Redundant original context survives high-frequency capture and missing feedback.
    InputSender high; CHECK(high.capture(in,4000)); CHECK(high.act(ActionKind::Jump,4000));
    for (uint64_t t=4001;t<4100;++t) CHECK(high.capture(in,t));
    auto h=high.batch(4100); CHECK(h.inputs.front().sequence==1 && h.inputs.back().sequence==100 && h.inputs.size()==8);
    // Nested times, not just transport headers, are mapped into server time.
    InputReceiver offset;
    CHECK(offset.receive(h,9100,[](uint64_t t)->std::optional<uint64_t>{return t+5000;}));
    CHECK(offset.drainActions(9100).at(0).localTime==9000);
    CHECK(offset.held(9100).createdMs==9099);
}
void snapshots()
{
    SnapshotSender sender(false); SnapshotReceiver receiver; auto w=makeWorld(512);
    CHECK(sender.capture(w,1)); auto invalid=w; invalid[10].kind=EntityKind::Mover; CHECK(!sender.capture(invalid,2)); CHECK(!sender.receive({99,0,0,false}));
    std::vector<SnapshotPart> sent;
    for (uint64_t t=1;t<=62;++t) { auto p=sender.poll(t); CHECK(p); sent.push_back(*p); }
    CHECK(sent.size()==62); CHECK(!sender.poll(63));
    std::reverse(sent.begin(),sent.end());
    for (auto p:sent) if (p.index!=7) CHECK(receiver.receive(p,100));
    CHECK(!receiver.candidate()); auto partial=receiver.feedback(100); CHECK(partial.receiving==1 && partial.installed==0);
    CHECK(sender.receive(partial)); CHECK(sender.baselineId()==0);
    auto repair=sender.poll(200); CHECK(repair && repair->index==7); CHECK(receiver.receive(*repair,220)); CHECK(receiver.candidate());
    CHECK(receiver.feedback(220).installed==0); CHECK(sender.receive(receiver.feedback(220))); CHECK(!sender.poll(221));
    CHECK(!receiver.commit(9)); CHECK(receiver.commit(1)); CHECK(receiver.installed()->world==w);
    CHECK(sender.receive(receiver.feedback(221))); CHECK(sender.baselineId()==1);
    w[0].position[0]=100; CHECK(sender.capture(w,2)); auto p=sender.poll(222); CHECK(p && p->baseline==1 && p->count==1);
    CHECK(receiver.receive(*p,240)); CHECK(receiver.candidate()->world==w); CHECK(receiver.commit(p->id)); CHECK(sender.receive(receiver.feedback(240)));
    CHECK(!receiver.receive(sent[0],241));
    // A conflicting duplicate of a decoded but uncommitted candidate is rejected.
    SnapshotReceiver complete; auto single=parts({1,0,1,makeWorld(1)})[0];
    CHECK(complete.receive(single,1)); single.bytes.back()^=1;
    CHECK(!complete.receive(single,2)); CHECK(!complete.candidate());
    // Lost install acknowledgement: repeats do not install twice or deadlock feedback.
    w[0].health=80; CHECK(sender.capture(w,3)); p=sender.poll(250); CHECK(p); CHECK(receiver.receive(*p,260)); CHECK(receiver.commit(p->id));
    auto repeated=sender.poll(351); CHECK(repeated); CHECK(!receiver.receive(*repeated,360)); CHECK(receiver.feedback(360).installed==p->id);
    CHECK(sender.receive(receiver.feedback(360))); CHECK(sender.baselineId()==p->id);
    // Receiver discards baseline state, requests a complete replacement.
    SnapshotReceiver fresh; w[0].health=79; CHECK(sender.capture(w,4)); p=sender.poll(370); CHECK(p && p->baseline!=0);
    CHECK(!fresh.receive(*p,380)); CHECK(fresh.feedback(380).requestFull); CHECK(sender.receive(fresh.feedback(380)));
    for (uint64_t t=390;t<500;++t)
    { auto q=sender.poll(t); if (q) { CHECK(q->baseline==0); CHECK(fresh.receive(*q,t)); } if (fresh.candidate()) break; }
    CHECK(fresh.candidate()); CHECK(fresh.commit(fresh.candidate()->id)); CHECK(fresh.installed()->world==w);
    // Conflicting fragments cannot be combined into a world.
    SnapshotReceiver conflict; auto fragments=parts({100,0,100,w}); CHECK(conflict.receive(fragments[0],1));
    fragments[0].bytes.back()^=1; CHECK(!conflict.receive(fragments[0],2)); CHECK(conflict.feedback(2).requestFull && !conflict.candidate());
    auto malformed=fragments[0]; malformed.total=UINT32_MAX; CHECK(!conflict.receive(malformed,3));
    // Partial expiry, stale tick and mismatched full payload/header are fail-closed.
    SnapshotReceiver expiry; CHECK(expiry.receive(parts({100,0,100,w})[0],0)); CHECK(expiry.feedback(SnapshotTransferTimeout).receiving==0);
    auto mismatch=parts({100,0,100,makeWorld(1)})[0]; mismatch.id=101; CHECK(!expiry.receive(mismatch,SnapshotTransferTimeout+1));
    SnapshotReceiver monotonic;
    for (auto q:parts({1,0,5,makeWorld(1)})) CHECK(monotonic.receive(q,1)); CHECK(monotonic.commit(1));
    for (auto q:parts({2,0,4,makeWorld(1)})) CHECK(!monotonic.receive(q,2));
    // A newer fragment must not destroy a nearly complete prior snapshot.
    // Both are authenticated, newer than installed, and can share a baseline.
    SnapshotReceiver interleaved;auto firstParts=parts({1,0,1,makeWorld(16)}),secondParts=parts({2,0,2,makeWorld(16)});
    CHECK(firstParts.size()>1 && firstParts.size()==secondParts.size());
    CHECK(interleaved.receive(firstParts[0],1));CHECK(interleaved.receive(secondParts[0],2));
    for(size_t i=1;i<firstParts.size();++i)CHECK(interleaved.receive(firstParts[i],3));
    CHECK(interleaved.candidate() && interleaved.candidate()->id==1);CHECK(interleaved.commit(1));
    for(size_t i=1;i<secondParts.size();++i)CHECK(interleaved.receive(secondParts[i],4));
    CHECK(interleaved.candidate() && interleaved.candidate()->id==2);CHECK(interleaved.commit(2));
    CHECK(!interleaved.receive(firstParts[0],5));
    SnapshotReceiver bounded;
    for(uint64_t id=1;id<=20;++id) {
        CHECK(bounded.receive(parts({id,0,id,makeWorld(16)})[0],id));
        CHECK(bounded.bufferedTransfers()<=SnapshotReceiver::MaxAssemblies);
    }
    CHECK(!bounded.receive(firstParts[0],21));
    CHECK(bounded.bufferedTransfers()==SnapshotReceiver::MaxAssemblies);
    auto newest=parts({20,0,20,makeWorld(16)});
    for(size_t i=1;i<newest.size();++i)CHECK(bounded.receive(newest[i],22));
    CHECK(bounded.candidate() && bounded.candidate()->id==20 && bounded.commit(20));
    CHECK(bounded.bufferedTransfers()==0 && !bounded.receive(secondParts[0],23));
    SnapshotReceiver independentExpiry;
    CHECK(independentExpiry.receive(firstParts[0],0));CHECK(independentExpiry.receive(secondParts[0],9000));
    CHECK(independentExpiry.feedback(10000).receiving==2 && independentExpiry.bufferedTransfers()==1);
    for(size_t i=1;i<secondParts.size();++i)CHECK(independentExpiry.receive(secondParts[i],10001));
    CHECK(independentExpiry.commit(2));
    SnapshotSender coalesced; auto reuse=makeWorld(1); CHECK(coalesced.capture(reuse,1)); CHECK(coalesced.poll(1));
    CHECK(coalesced.capture({},2)); reuse[0].kind=EntityKind::Mover;
    CHECK(!coalesced.capture(reuse,3)); reuse[0].ref.generation=2; CHECK(coalesced.capture(reuse,3));
    // Sender retains only bounded history and falls back to full if no installed base survives.
    SnapshotSender stalled; CHECK(stalled.capture(makeWorld(1),1)); auto initial=stalled.poll(0); CHECK(initial);
    for (uint64_t t=SnapshotTransferTimeout;t<=SnapshotTransferTimeout*40;t+=SnapshotTransferTimeout) CHECK(stalled.poll(t));
    CHECK(!stalled.receive({1,0,0,false}));
    // Small snapshots pipeline against an installed base, without waiting for
    // each RTT. A late ACK remains valid, and loss cannot grow the flight window.
    SnapshotSender pipeline;SnapshotReceiver piped;auto small=makeWorld(1);
    CHECK(pipeline.capture(small,1));auto boot=pipeline.poll(1);CHECK(boot);
    CHECK(piped.receive(*boot,1) && piped.commit(boot->id));CHECK(pipeline.receive(piped.feedback(1)));
    CHECK(pipeline.capture(small,2));auto second=pipeline.poll(34);CHECK(second && second->baseline==boot->id);
    CHECK(pipeline.capture(small,3));auto third=pipeline.poll(67);CHECK(third && third->id>second->id && third->baseline==boot->id);
    CHECK(piped.receive(*second,80) && piped.commit(second->id));CHECK(pipeline.receive(piped.feedback(80)));
    CHECK(piped.receive(*third,100) && piped.commit(third->id));CHECK(pipeline.receive(piped.feedback(100)));
    for(uint64_t n=4;n<100;++n){CHECK(pipeline.capture(small,n));(void)pipeline.poll(n*34);}
    CHECK(pipeline.activeId()<=third->id+16);

    // A moving 16-player world changes equipment as well as pawns, exceeding
    // four fragments. Delay installation ACKs and lose one complete delta;
    // a subsequent delta must still install against the retained base.
    SnapshotSender crowded(false);SnapshotReceiver crowd;auto crowdWorld=makeWorld(64);
    CHECK(crowded.capture(crowdWorld,1));
    for(uint64_t t=1;t<20;++t)if(auto part=crowded.poll(t))CHECK(crowd.receive(*part,t));
    CHECK(crowd.candidate() && crowd.commit(crowd.candidate()->id));CHECK(crowded.receive(crowd.feedback(20)));
    for(auto& e:crowdWorld)++e.position[0];CHECK(crowded.capture(crowdWorld,2));
    std::vector<SnapshotPart> lost;
    for(uint64_t t=40;t<60;++t)if(auto part=crowded.poll(t))lost.push_back(*part);
    CHECK(lost.size()>4);
    for(auto& e:crowdWorld)++e.position[0];CHECK(crowded.capture(crowdWorld,3));
    std::vector<SnapshotPart> recovery;
    for(uint64_t t=80;t<100;++t)if(auto part=crowded.poll(t))recovery.push_back(*part);
    CHECK(!recovery.empty() && recovery[0].id>lost[0].id);
    std::reverse(recovery.begin(),recovery.end());
    for(const auto& part:recovery)CHECK(crowd.receive(part,150));
    CHECK(crowd.candidate() && crowd.candidate()->world==crowdWorld);
    CHECK(crowd.commit(crowd.candidate()->id) && crowded.receive(crowd.feedback(151)));

    // Compressed full worlds and deltas survive reordering and selective loss.
    SnapshotSender compact;SnapshotReceiver expanded;auto compactWorld=makeWorld(512);
    CHECK(compact.capture(compactWorld,1));std::vector<SnapshotPart> packed;
    for(uint64_t t=1;t<100;++t)if(auto q=compact.poll(t))packed.push_back(*q);
    CHECK(packed.size()<31 && packed.front().bytes[2]=='Z');
    std::reverse(packed.begin(),packed.end());
    for(auto q:packed)if(q.index!=2)CHECK(expanded.receive(q,100));
    CHECK(!expanded.candidate());CHECK(compact.receive(expanded.feedback(100)));
    auto missing=compact.poll(200);CHECK(missing && missing->index==2);
    CHECK(expanded.receive(*missing,201) && expanded.candidate()->world==compactWorld);
    CHECK(expanded.commit(1) && compact.receive(expanded.feedback(202)));
    for(auto& e:compactWorld)++e.position[0];CHECK(compact.capture(compactWorld,2));
    for(uint64_t t=210;t<300;++t)if(auto q=compact.poll(t))CHECK(expanded.receive(*q,t));
    CHECK(expanded.candidate() && expanded.candidate()->world==compactWorld);
    CHECK(expanded.commit(expanded.candidate()->id));
    // Truncation, expansion overflow, oversized lengths and trailing runs fail closed.
    auto one=makeWorld(1);SnapshotSender compactOne;CHECK(compactOne.capture(one,1));auto q=compactOne.poll(1);CHECK(q && q->count==1 && q->bytes[2]=='Z');
    auto reject=[&](Bytes bytes) { SnapshotReceiver r;auto altered=*q;altered.bytes=std::move(bytes);altered.total=uint32_t(altered.bytes.size());CHECK(!r.receive(altered,1) && !r.candidate()); };
    for(size_t n=31;n<q->bytes.size();++n)reject(Bytes(q->bytes.begin(),q->bytes.begin()+n));
    auto badPacked=q->bytes;badPacked[4]=255;reject(badPacked);
    badPacked=q->bytes;badPacked[7]=31;reject(badPacked);
    badPacked=q->bytes;badPacked.push_back(0xff);reject(badPacked);

    // Reliable despawn may overtake a full snapshot. Rejecting that exact
    // transfer must yield a fresh world immediately, without a 10s stall;
    // repeated feedback must not cancel the replacement bootstrap.
    SnapshotSender staleFull;SnapshotReceiver lifeCycle;auto beforeDeath=makeWorld(1);
    CHECK(staleFull.capture(beforeDeath,1));auto stalePart=staleFull.poll(1);CHECK(stalePart);
    CHECK(lifeCycle.receive(*stalePart,2) && lifeCycle.candidate());lifeCycle.resync();
    auto rejected=lifeCycle.feedback(3);CHECK(rejected.receiving==stalePart->id && !rejected.receivedMask && rejected.requestFull);
    CHECK(!lifeCycle.receive(*stalePart,4));CHECK(staleFull.capture({},2) && staleFull.receive(rejected));
    auto replacement=staleFull.poll(5);CHECK(replacement && replacement->id>stalePart->id && !replacement->baseline);
    CHECK(staleFull.receive(rejected) && staleFull.activeId()==replacement->id);
    CHECK(lifeCycle.receive(*replacement,6) && lifeCycle.candidate()->world.empty());
    CHECK(lifeCycle.commit(replacement->id) && staleFull.receive(lifeCycle.feedback(7)));

    // Obsolete lifecycle candidates do not invalidate an installed baseline.
    // Replacing only the rejected transfer avoids full-world storms on deaths.
    SnapshotSender lifeSender;SnapshotReceiver lifeReceiver;auto lifeWorld=makeWorld(1);
    CHECK(lifeSender.capture(lifeWorld,1));auto lifeBoot=lifeSender.poll(1);CHECK(lifeBoot);
    CHECK(lifeReceiver.receive(*lifeBoot,2) && lifeReceiver.commit(lifeBoot->id) && lifeSender.receive(lifeReceiver.feedback(3)));
    lifeWorld[0].health=90;CHECK(lifeSender.capture(lifeWorld,2));auto obsolete=lifeSender.poll(4);CHECK(obsolete && obsolete->baseline);
    CHECK(lifeReceiver.receive(*obsolete,5));lifeReceiver.discardObsolete();auto obsoleteFeedback=lifeReceiver.feedback(6);
    CHECK(!obsoleteFeedback.requestFull && obsoleteFeedback.receiving==obsolete->id && !obsoleteFeedback.receivedMask);
    CHECK(lifeSender.receive(obsoleteFeedback));lifeWorld[0].health=50;CHECK(lifeSender.capture(lifeWorld,3));
    auto freshDelta=lifeSender.poll(7);CHECK(freshDelta && freshDelta->baseline==lifeBoot->id && freshDelta->id>obsolete->id);
    CHECK(!lifeReceiver.receive(*obsolete,8));CHECK(lifeReceiver.receive(*freshDelta,9) && lifeReceiver.candidate()->world==lifeWorld);
    CHECK(lifeReceiver.commit(freshDelta->id) && lifeSender.receive(lifeReceiver.feedback(10)));

    // Snapshot transport delivers fragments out of order, independently of state replacement.
    Transport transport(1,1); Bytes wire;
    for (uint64_t id:{2,1})
    { CHECK(encode(Packet{1,1,id,{},Message{Kind::SnapshotPart,Lane::Snapshots,id,1,100,{uint8_t(id)}}},wire)); CHECK(transport.receiveTrusted(wire,2)==Receive::Accepted); }
    CHECK(transport.drain().size()==2);
    CHECK(encode(Packet{1,1,3,{},Message{Kind::SnapshotPart,Lane::Snapshots,1,1,100,{1}}},wire)); CHECK(transport.receiveTrusted(wire,3)==Receive::Accepted); CHECK(transport.drain().empty());
}
struct Datagram { uint64_t at; bool toServer; Bytes bytes; };
void integration(unsigned seed)
{
    std::mt19937 rng(seed); auto key=generateInvitationKey();
    SecureSession server(true,key,"replication",{1,2},7),client(false,key,"replication",{1,3},7);
    ReplicationPeer host(server,testMatch()),player(client,testMatch(),{11,1}); std::vector<Datagram> link;
    auto target=makeWorld(512); CHECK(host.snapshots.capture(target,1));
    std::set<uint64_t> applied,accounted; size_t submitted=0, outcomes=0, unconfirmed=0; bool loaded=false, changed=false; uint64_t lastCapture=0, lastAction=0;
    std::vector<CombatOutcome> committed; bool sentKick=false,sentBash=false,sentSound=false;std::set<uint64_t> soundIds;
    CombatOutcome kick{1,420,1,{1,1},{2,1},1,1,CombatMove::Kick,CombatContact::Blocked,100,false};
    CombatOutcome bash{2,426,2,{1,1},{2,1},1,1,CombatMove::Bash,CombatContact::Hit,98,true};
    auto send=[&](SecureSession& from,bool toServer,uint64_t t)
    {
        for (auto bytes:from.takeDatagrams())
        {
            CHECK(bytes.size()<=1200);
            if (rng()%100<5 || (t>=6000 && t<6250)) continue;
            auto at=t+10+rng()%35;
            if (rng()%100<5) link.push_back({at+10,toServer,bytes});
            link.push_back({at,toServer,std::move(bytes)});
        }
    };
    for (uint64_t t=0;t<18000;t+=5)
    {
        // Intentional delivery reordering: process by queued position, not deadline order.
        for (auto it=link.begin();it!=link.end();)
        {
            if (it->at>t) { ++it; continue; }
            // Early application records may overtake the final handshake flight;
            // the secure channel intentionally drops them until authenticated.
            (it->toServer?server:client).receiveDatagram(it->bytes);
            CHECK(!server.failed() && !client.failed());
            it=link.erase(it);
        }
        server.step(t+10000); client.step(t+50000); CHECK(!server.failed() && !client.failed());
        if (player.world.candidate()) { CHECK(player.world.commit(player.world.candidate()->id)); if (!loaded) std::cout<<"loaded seed="<<seed<<" at="<<t<<"ms\n"; loaded=true; }
        if (loaded && t>lastCapture+20 && t<12000)
        {
            Input in; in.life=1; in.forward=600; in.held=t%400<200?2:0;
            CHECK(player.inputs.capture(in,t+50000)); lastCapture=t;
            if (t-lastAction>=250) { CHECK(player.inputs.act(ActionKind::Kick,t+50000)); ++submitted; lastAction=t; }
        }
        if (loaded && !changed && t>=7000)
        {
            target[0].health=75; target[0].position[0]=1024; target.back().ref.generation=2;
            CHECK(host.snapshots.capture(target,2)); changed=true;
        }
        if (loaded && t>=7000 && !sentKick)
        { CHECK(!player.publishCombat(kick)); if(host.publishCombat(kick)){CHECK(!host.publishCombat(kick));sentKick=true;} }
        if (sentKick && t>=7100 && !sentBash)
        { sentBash=host.publishCombat(bash); }
        if(loaded && t>=7000 && !sentSound) {
            SoundCue sound;sound.emitter=1;sound.object=1;sound.package="RuneSFX";host.publishSound(sound);sentSound=true;
        }
        host.step(); player.step();
        for(const auto& sound:player.drainSounds())CHECK(soundIds.insert(sound.id).second && sound.package=="RuneSFX");
        auto received=player.drainCombat(); committed.insert(committed.end(),received.begin(),received.end());
        for (auto a:host.commands.drainActions(t+10000)) { CHECK(applied.insert(a.action.id).second); CHECK(a.localTime<=t+10000 && t+10000-a.localTime<=250); }
        for(auto result:player.inputs.drainResults()){++outcomes;accounted.insert(result.id);}
        for(auto id:player.inputs.drainUnconfirmed()){++unconfirmed;accounted.insert(id);}
        send(server,false,t); send(client,true,t);
        CHECK(link.size()<512 && server.snapshotPartsPending()<=4);
    }
    CHECK(host.matchAgreed() && player.matchAgreed() && loaded && changed);
    CHECK(host.characterChoice()==openrune::CharacterChoice{11,1});
    CHECK(player.world.installed() && player.world.installed()->world==target);
    CHECK(host.snapshots.baselineId()==player.world.installed()->id); CHECK(!applied.empty() && submitted>10);
    std::cout<<"RESULT_COUNTS seed="<<seed<<" submitted="<<submitted<<" outcomes="<<outcomes<<" unconfirmed="<<unconfirmed<<" pending="<<player.inputs.pending()<<std::endl;
    // Unconfirmed is provisional: a late authenticated admission can update
    // that same action (explicitly covered in inputActions). Count IDs, not
    // the sum of provisional and confirmed notifications.
    CHECK(accounted.size()==submitted);for(uint64_t id=1;id<=submitted;++id)CHECK(accounted.contains(id)); CHECK(player.inputs.pending()==0); CHECK(host.commands.held(28000).held==0);
    CHECK(sentSound && soundIds.size()<=1);
    CHECK(committed==std::vector<CombatOutcome>({kick,bash}) && !player.combatOverflowed());
    std::cout<<"seed="<<seed<<" submitted="<<submitted<<" admitted="<<applied.size()<<" confirmed="<<outcomes<<" unconfirmed="<<unconfirmed<<" snapshot="<<player.world.installed()->id<<'\n';
}
// Exercise the authenticated wire path, including a lost first packet and
// duplicate delivery. Old 50-ms feedback batching delayed/dropped sound bursts.
void sameFrameActions(bool legacy=false)
{
    auto key=generateInvitationKey();
    SecureSession server(true,key,"input-frame",{1,2},7),client(false,key,"input-frame",{1,3},7);
    ReplicationPeer host(server,testMatch()),player(client,testMatch());
    CHECK(host.snapshots.capture(makeWorld(2),1));
    std::map<uint64_t,uint64_t> presses;std::set<uint64_t> received;uint64_t maxDelay=0;
    for(uint64_t t=0;t<4000;++t) {
        server.step(t+10000);host.step();
        for(auto action:host.commands.drainActions(t+10000)) {
            CHECK(presses.contains(action.action.id) && received.insert(action.action.id).second);
            maxDelay=std::max(maxDelay,t-presses.at(action.action.id));
            CHECK(action.action.createdMs==presses.at(action.action.id)+50000);
            CHECK(action.input.held==(action.action.kind==ActionKind::RaiseShield?2:0));
        }
        if(t%8==0) { // 125-Hz rendering, independent 1-ms server pump.
            client.step(t+50000,legacy);player.step(legacy);
            if(auto* world=player.world.candidate())CHECK(player.world.commit(world->id));
            if(t==3008 || t==3024 || t==3040) {
                CHECK(player.world.installed());Input in;in.life=1;in.held=t==3008?2:0;
                CHECK(player.inputs.capture(in,t+50000));
                auto id=player.inputs.act(t==3008?ActionKind::RaiseShield:t==3024?ActionKind::LowerShield:ActionKind::Attack,t+50000);
                CHECK(id);presses[*id]=t;
            }
            if(!legacy){player.flushInputs();client.flush();}
            for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
        }
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
    }
    CHECK(received.size()==3 && (legacy?maxDelay>=16:maxDelay<=8));
    std::cout<<"SAME_FRAME_ACTIONS legacy="<<legacy<<" count="<<received.size()<<" max_dispatch_to_admission_ms="<<maxDelay<<'\n';
}
void soundDelivery(bool enforce=true)
{
    auto key=generateInvitationKey();
    SecureSession server(true,key,"sound-latency",{1,2},7),client(false,key,"sound-latency",{1,3},7);
    ReplicationPeer host(server,testMatch()),player(client,testMatch());
    std::map<uint64_t,uint64_t> published,received;
    uint64_t nextId=1,singleMax=0,burstMax=0;bool dropNext=false,dropped=false;
    auto publish=[&](uint64_t t) {
        SoundCue cue;cue.emitter=1;cue.object=1;cue.package="RuneSFX";
        host.publishSound(cue);published[nextId++]=t;
    };
    for(uint64_t t=0;t<8500;++t) {
        server.step(t+10000);client.step(t+50000);
        CHECK(!server.failed() && !client.failed());
        if(t>=3000 && t<6070 && (t-3000)%307==0) {
            CHECK(host.matchAgreed() && player.matchAgreed());publish(t);
            if(t==3000)dropNext=true;
        }
        if(t==7000)for(int i=0;i<32;++i)publish(t);
        host.step();player.step();
        for(const auto& cue:player.drainSounds()) {
            CHECK(published.contains(cue.id) && !received.contains(cue.id));
            received[cue.id]=t;
            auto delay=t-published.at(cue.id);
            if(published.at(cue.id)==7000)burstMax=std::max(burstMax,delay);
            else singleMax=std::max(singleMax,delay);
        }
        for(auto bytes:server.takeDatagrams()) {
            if(dropNext){dropNext=false;dropped=true;continue;}
            client.receiveDatagram(bytes);client.receiveDatagram(bytes);
        }
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
    }
    std::cout<<"SOUND_DELIVERY published="<<published.size()<<" received="<<received.size()
        <<" single_max_ms="<<singleMax<<" burst_max_ms="<<burstMax<<" dropped_first="<<dropped<<'\n';
    if(enforce) {
        CHECK(dropped && received.size()==published.size());
        CHECK(singleMax<=36 && burstMax<=246);
    }
}
void combatBackpressure()
{
    auto key=generateInvitationKey();
    SecureSession server(true,key,"combat-limit",{1,2},7),client(false,key,"combat-limit",{1,3},7);
    ReplicationPeer host(server,testMatch()),player(client,testMatch());
    uint64_t next=1;
    for(uint64_t t=0;t<20000 && !player.combatOverflowed();t+=5)
    {
        server.step(t+10000);client.step(t+50000);
        if(server.ready() && client.ready() && t%50==0 && next<=65)
        {
            CombatOutcome c{next,next,0,{1,1},{2,1},1,1,CombatMove::Kick,CombatContact::Blocked,100,false};
            if(host.publishCombat(c))++next;
        }
        host.step();player.step();
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
        CHECK(!server.failed() && !client.failed());
    }
    CHECK(next==66 && player.combatOverflowed() && player.drainCombat().empty());
}
void incompatibleMatch(int mismatch=0)
{
    auto key=generateInvitationKey();
    SecureSession server(true,key,"match-gate",{1,2},7),client(false,key,"match-gate",{1,3},7);
    auto other=testMatch();if(mismatch==1)other.mode=GameMode::TeamDeathmatch;else if(mismatch==2)++other.rulesRevision;else if(mismatch==3)other.gameplayHash[0]^=1;else other.mapHash[0]^=1;
    ReplicationPeer host(server,testMatch()),player(client,other);
    CHECK(host.snapshots.capture(makeWorld(2),1));
    for(uint64_t t=0;t<5000 && !host.matchMismatch() && !player.matchMismatch();t+=5)
    {
        server.step(t+10000);client.step(t+50000);host.step();player.step();
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
        CHECK(!server.failed() && !client.failed());
    }
    CHECK(host.matchMismatch() || player.matchMismatch());
    CHECK(!host.matchAgreed() && !player.matchAgreed() && !player.world.candidate());
}
void closeSession()
{
    InvitationKey key{};key[0]=42;
    SecureSession server(true,key,"close",{1,2},1),client(false,key,"close",{1,3},1);
    ReplicationPeer host(server,testMatch()),player(client,testMatch());
    bool requested=false;
    for(uint64_t t=0;t<5000 && !host.remoteClosed();t+=5)
    {
        server.step(t+10000);client.step(t+50000);host.step();player.step();
        if(player.matchAgreed() && !requested){player.close(CloseReason::Leave);requested=true;}
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
    }
    CHECK(requested && host.remoteClosed()==CloseReason::Leave);
    CombatOutcome result{1,1,0,{1,1},{2,1},1,1,CombatMove::Kick,CombatContact::Hit,93,false};
    CHECK(!host.publishCombat(result));
    Bytes wire;CloseReason decoded;
    CHECK(encodeClose(7,CloseReason::MatchEnded,wire) && decodeClose(7,wire,decoded) && decoded==CloseReason::MatchEnded);
    CHECK(!decodeClose(8,wire,decoded));wire.back()=3;CHECK(!decodeClose(7,wire,decoded));
}
void matchTravel() {
    MatchStatus state{MatchPhase::Intermission,8,20,2,{{1,-1,2},{2,20,1},{3,4,4},{4,4,0}}},decoded;
    state.weatherSeed=0x8e71a91d;state.weatherStartMs=123456789;
    Bytes b;CHECK(encodeMatchStatus(7,state,b) && decodeMatchStatus(7,b,decoded) && state==decoded);
    for(size_t n=0;n<b.size();++n)CHECK(!decodeMatchStatus(7,std::span(b).first(n),decoded) && decoded==state);
    CHECK(!decodeMatchStatus(8,b,decoded));auto extra=b;extra.push_back(0);CHECK(!decodeMatchStatus(7,extra,decoded));
    auto weatherBad=state;weatherBad.weatherSeed=0;CHECK(!encodeMatchStatus(7,weatherBad,b));
    weatherBad=state;weatherBad.weatherStartMs=0;CHECK(!encodeMatchStatus(7,weatherBad,b));
    weatherBad=state;weatherBad.weatherStartMs=uint64_t(INT64_MAX);CHECK(!encodeMatchStatus(7,weatherBad,b));
    auto bad=state;bad.scores[1].player=1;CHECK(!encodeMatchStatus(7,bad,b));
    bad=state;bad.phase=MatchPhase::Playing;CHECK(!encodeMatchStatus(7,bad,b));
    auto arena=state;arena.mode=GameMode::Arena;arena.arenaPhase=2;arena.arenaRound=3;arena.arenaCountdown=10;
    arena.scores[0].queue=1;arena.scores[0].team=0;
    CHECK(encodeMatchStatus(7,arena,b) && decodeMatchStatus(7,b,decoded) && decoded==arena);
    arena.arenaPhase=4;CHECK(!encodeMatchStatus(7,arena,b));arena.arenaPhase=2;
    arena.scores[0].queue=0;CHECK(!encodeMatchStatus(7,arena,b));arena.scores[0].queue=17;CHECK(!encodeMatchStatus(7,arena,b));
    auto team=state;team.mode=GameMode::TeamDeathmatch;team.winner=0;team.winningTeam=0;team.teamScores={20,19};
    for(size_t i=0;i<team.scores.size();++i)team.scores[i].team=uint8_t(i%2);
    CHECK(encodeMatchStatus(7,team,b) && decodeMatchStatus(7,b,decoded) && team==decoded);
    auto headball=team;headball.mode=GameMode::HeadBall;headball.headBallStarted=true;headball.readyTeams={1,1};
    CHECK(encodeMatchStatus(7,headball,b) && decodeMatchStatus(7,b,decoded) && decoded==headball);
    headball.headBallCountdown=1;CHECK(!encodeMatchStatus(7,headball,b));headball.headBallStarted=false;
    CHECK(encodeMatchStatus(7,headball,b));headball.readyTeams[0]=2;CHECK(!encodeMatchStatus(7,headball,b));
    headball.readyTeams[0]=1;headball.headBallCountdown=121;CHECK(!encodeMatchStatus(7,headball,b));
    auto four=team;four.teams=4;four.winningTeam=3;four.teamScores={1,2,3,4};
    for(size_t i=0;i<four.scores.size();++i){four.scores[i].team=uint8_t(i);four.scores[i].pawn={uint32_t(40+i),2};}
    CHECK(encodeMatchStatus(7,four,b) && decodeMatchStatus(7,b,decoded) && decoded==four);
    four.scores[0].pawn.generation=0;CHECK(!encodeMatchStatus(7,four,b));
    auto badTeam=team;badTeam.scores[0].team=255;CHECK(!encodeMatchStatus(7,badTeam,b));
    badTeam=team;badTeam.winningTeam=2;CHECK(!encodeMatchStatus(7,badTeam,b));
    badTeam=team;badTeam.phase=MatchPhase::Playing;CHECK(!encodeMatchStatus(7,badTeam,b));
    auto next=testMatch();next.map="DM-Thorstadt";next.mapHash[0]^=1;
    MapTravel travel{8,next},copy;CHECK(encodeMapTravel(7,travel,b) && decodeMapTravel(7,b,copy) && copy.epoch==8 && copy.match==next);
    for(size_t n=0;n<b.size();++n)CHECK(!decodeMapTravel(7,std::span(b).first(n),copy));
    auto key=generateInvitationKey();
    SecureSession server(true,key,"travel",{1,2},7),client(false,key,"travel",{1,3},7);
    ReplicationPeer host(server,testMatch()),player(client,testMatch(),{35,0});
    auto first=makeWorld(3),second=first;second[0].position[0]=5000;
    CHECK(host.snapshots.capture(first,1));
    bool began=false,accepted=false,staleSent=false,statusSent=false;uint64_t connection=0,requestedAt=0;
    for(uint64_t t=0;t<10000;t+=5) {
        server.step(t+10000);client.step(t+50000);host.step();player.step();
        if(player.world.candidate())CHECK(player.world.commit(player.world.candidate()->id));
        if(!began && player.world.installed() && host.snapshots.baselineId()) {
            connection=server.sessionId();CHECK(connection!=0);CHECK(!player.travel(next));
            CHECK(host.travel(next));CHECK(!host.matchAgreed());CHECK(host.snapshots.capture(second,1));began=true;
        }
        if(player.requestedTravel()) {
            if(!requestedAt)requestedAt=t;
            // Keep the engine loading asynchronously for 500ms; no old inputs/worlds may escape.
            if(t-requestedAt>=500) {
                CHECK(player.requestedTravel()->match==next && player.requestedTravel()->epoch==8);
                CHECK(player.acceptTravel(next));CHECK(!player.world.installed());accepted=true;
            }
        }
        if(accepted && host.matchAgreed() && !staleSent) {
            Bytes old;CHECK(encodeMatchContract(7,testMatch(),old));CHECK(server.enqueue(Lane::Control,std::move(old),0));
            ActorEvent actor{1,{1,1},ActorOperation::Spawn,EntityKind::Pawn,"RuneI","Ragnar","",""};
            CHECK(encodeActorEvent(7,actor,old));CHECK(server.enqueue(Lane::Control,std::move(old),0));
            CHECK(encodeMatchStatus(7,state,old));CHECK(server.enqueue(Lane::Control,std::move(old),0));
            InputBatch batch;batch.inputs.push_back({1,t+50000,1,900,0,0,0,0});
            CHECK(encodeInputs(7,batch,old));CHECK(client.enqueue(Lane::Gameplay,std::move(old),250));
            staleSent=true;
        }
        if(accepted && !statusSent && host.matchAgreed()){CHECK(host.publishStatus(state));statusSent=true;}
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
        CHECK(!server.failed() && !client.failed() && !host.matchMismatch() && !player.matchMismatch());
    }
    CHECK(began && accepted && staleSent && statusSent && host.mapEpoch()==8 && player.mapEpoch()==8);
    CHECK(host.characterChoice()==openrune::CharacterChoice{35,0});
    CHECK(connection==server.sessionId());CHECK(player.world.installed() && player.world.installed()->world==second);
    CHECK(player.status() && *player.status()==state && host.commands.held(20000).forward==0);
    CHECK(!player.actorOverflowed() && !player.combatOverflowed() && player.drainActors().empty());
    CHECK(host.travel(testMatch()));
    // Leaving during a map barrier must still get through, without match agreement.
    player.close(CloseReason::Leave);
    for(uint64_t t=10000;t<12000 && !host.remoteClosed();t+=5) {
        server.step(t+10000);client.step(t+50000);host.step();player.step();
        for(auto bytes:server.takeDatagrams())client.receiveDatagram(bytes);
        for(auto bytes:client.takeDatagrams())server.receiveDatagram(bytes);
    }
    CHECK(host.remoteClosed()==CloseReason::Leave);
}
void playerIdentity(){
 CHECK(validPlayerName("Eirik Ashwolf")&&validPlayerName("Øystein"));
 for(auto bad:{"", " [BOT] Fake", "[BOT] Fake", "A\nB", "A\x1b" "B", "A\xe2\x80\xae" "B"})CHECK(!validPlayerName(bad));
 CHECK(!validPlayerName(std::string(25,'x'))&&!validPlayerName(std::string("A\xc0\x80",3)));
 for(uint32_t seed=1;seed<=100;++seed){std::set<std::string> pool;for(uint32_t slot=1;slot<=64;++slot){auto name=botName(slot,seed);CHECK(validPlayerName(name)&&pool.insert(name).second&&name==botName(slot,seed));}}
 CHECK(botName(1,1)!=botName(1,2));
 std::set<std::string> names;
 for(uint32_t i=1;i<=16;++i){auto name=botName(i);CHECK(validPlayerName(name)&&names.insert(name).second);PlayerLabel l{i,name,true},decoded;Bytes b;CHECK(encodePlayerLabel(7,l,b)&&b.size()<=MaxPayloadBytes&&decodePlayerLabel(7,b,decoded)&&l==decoded);CHECK(labelText(l,false)==name&&labelText(l,true)=="[BOT] "+name);for(size_t n=0;n<b.size();++n)CHECK(!decodePlayerLabel(7,std::span(b).first(n),decoded));}
 PlayerProfile profile{"Sigrun",false},decoded;Bytes b;CHECK(encodePlayerProfile(7,profile,b)&&decodePlayerProfile(7,b,decoded)&&profile==decoded);for(size_t n=0;n<b.size();++n)CHECK(!decodePlayerProfile(7,std::span(b).first(n),decoded));
 auto extra=b;extra.push_back(0);CHECK(!decodePlayerProfile(7,extra,decoded));CHECK(!decodePlayerProfile(8,b,decoded));
 auto key=generateInvitationKey();Bytes address={2,3};SecureSession server(true,key,"names",address),client(false,key,"names",address);ReplicationPeer host(server,testMatch()),guest(client,testMatch(),{},profile);
 bool renamed=false;for(uint64_t now=1;now<=4500;now+=10){server.step(now);client.step(now);host.step();guest.step();for(auto v:server.takeDatagrams())client.receiveDatagram(v);for(auto v:client.takeDatagrams())server.receiveDatagram(v);
  if(host.matchAgreed()&&host.playerProfile()){host.publishPlayerLabel({1,host.playerProfile()->name,false});}
  if(!renamed&&now>2500){CHECK(guest.playerLabel(1)&&guest.playerLabel(1)->name=="Sigrun");CHECK(guest.setPlayerProfile({"Brynja",false}));renamed=true;}
 }
 CHECK(host.playerProfile()&&host.playerProfile()->name=="Brynja"&&guest.playerLabel(1)&&guest.playerLabel(1)->name=="Brynja");
}
Bytes unhex(const std::string& s)
{
    if (s.size()%2) return {}; Bytes b;
    for (size_t i=0;i<s.size();i+=2) b.push_back(uint8_t(std::stoul(s.substr(i,2),nullptr,16))); return b;
}
void hex(const Bytes& b) { const char* h="0123456789abcdef"; for(auto v:b) std::cout<<h[v>>4]<<h[v&15]; std::cout<<'\n'; }
}
int main(int argc,char** argv)
{
    try
    {
        if (argc>1 && std::string(argv[1])=="decode")
        {
            std::string line;
            while (std::getline(std::cin,line))
            {
                auto b=unhex(line); Bytes out; InputBatch i; InputFeedback f; SnapshotPart p; Snapshot s; CombatOutcome c; MatchContract m; ActorEvent a; ActionResult d; CloseReason close; MatchStatus status;MapTravel travel;PlayerProfile profile;PlayerLabel label;std::vector<SoundCue> sounds;
                if ((decodePlayerProfile(7,b,profile)&&encodePlayerProfile(7,profile,out)) || (decodePlayerLabel(7,b,label)&&encodePlayerLabel(7,label,out)) || (decodeSoundFeedback(7,b,f,sounds) && encodeSoundFeedback(7,f,sounds,out)) || (decodeInputs(7,b,i) && encodeInputs(7,i,out)) || (decodeFeedback(7,b,f) && encodeFeedback(7,f,out)) ||
                    (decodePart(7,b,p) && encodePart(7,p,out)) || (decodeCombatOutcome(7,b,c) && encodeCombatOutcome(7,c,out)) ||
                    (decodeMatchContract(7,b,m) && encodeMatchContract(7,m,out)) ||
                    (decodeActorEvent(7,b,a) && encodeActorEvent(7,a,out)) ||
                    (decodeActionDecision(7,b,d) && encodeActionDecision(7,d,out)) ||
                    (decodeMatchStatus(7,b,status) && encodeMatchStatus(7,status,out)) ||
                    (decodeMapTravel(7,b,travel) && encodeMapTravel(7,travel,out)) ||
                    (decodeClose(7,b,close) && encodeClose(7,close,out)) ||
                    (decodeSnapshot(b,nullptr,s) && encodeSnapshot(s,nullptr,out))) hex(out);
                else std::cout<<"invalid\n";
            }
            return 0;
        }
        if(argc>1 && std::string(argv[1])=="sound-profile") {soundDelivery(false);return 0;}
        codecs(); inputActions(); snapshots();playerIdentity();
        if (argc==1) { for (unsigned seed=1;seed<=10;++seed) integration(seed); sameFrameActions(true); sameFrameActions(); soundDelivery(); combatBackpressure(); incompatibleMatch(); incompatibleMatch(1); incompatibleMatch(2); incompatibleMatch(3); closeSession(); matchTravel(); }
        std::cout<<"PASS replication "<<checks<<" checks\n";
    }
    catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
