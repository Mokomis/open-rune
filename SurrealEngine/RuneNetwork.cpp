#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "OpenRuneVersion.h"
#include "RunePhysics.h"
#include "RuneIK.h"
#include "RuneNetwork.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Engine.h"
#include "Math/MovementAcceleration.h"
#include "Collision/TopLevel/TraceTest.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "RuneKick.h"
#include "RuneLeg.h"
#include "RuneSpider.h"
#include "RuneFinisher.h"
#include "RuneBash.h"
#include "RuneDualWield.h"
#include "RuneBloodlust.h"
#include "RuneParticles.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "openrune/net/MeleeTiming.h"
#include <iostream>
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include <set>
#include "Packages/Engine/Resources/USound.h"
#include <limits>
#include <filesystem>
#include <fstream>
#include <mbedtls/sha256.h>

using namespace openrune::net;
namespace
{
std::array<uint8_t,32> hashFiles(const std::vector<std::filesystem::path>& files)
{
    if(files.empty())throw std::runtime_error("missing gameplay content files");
    mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    if(mbedtls_sha256_starts(&sha,0))throw std::runtime_error("SHA-256 initialization failed");
    std::array<char,65536> buffer{};
    for(const auto& path:files)
    {
        auto filename=path.filename().string();
        uint64_t size=std::filesystem::file_size(path);
        if(filename.size()>255 || mbedtls_sha256_update(&sha,reinterpret_cast<const unsigned char*>(filename.data()),filename.size()))
            throw std::runtime_error("invalid gameplay file name");
        const uint8_t separator=0;
        if(mbedtls_sha256_update(&sha,&separator,1))throw std::runtime_error("SHA-256 failed");
        std::array<uint8_t,8> encodedSize{};
        for(int i=7;i>=0;--i){encodedSize[i]=uint8_t(size);size>>=8;}
        if(mbedtls_sha256_update(&sha,encodedSize.data(),encodedSize.size()))throw std::runtime_error("SHA-256 failed");
        std::ifstream file(path,std::ios::binary);
        if(!file)throw std::runtime_error("cannot read gameplay file: "+path.string());
        while(file)
        {
            file.read(buffer.data(),buffer.size());auto n=file.gcount();
            if(n>0 && mbedtls_sha256_update(&sha,reinterpret_cast<const unsigned char*>(buffer.data()),size_t(n)))
                throw std::runtime_error("SHA-256 failed");
        }
        if(!file.eof())throw std::runtime_error("gameplay file read failed: "+path.string());
    }
    std::array<uint8_t,32> digest{};
    if(mbedtls_sha256_finish(&sha,digest.data()))throw std::runtime_error("SHA-256 finalization failed");
    mbedtls_sha256_free(&sha);return digest;
}
bool replicatedEffect(UActor* a) {
    if(a->Class->package->GetPackageName()!=NameString("RuneI") || !RuneParticles::IsSystem(a))return false;
    static const std::set<NameString> names={"BloodSpray","GreenBloodSpray","BloodMist","SarkBloodMist","BloodSpurt","BloodUnderwater","HitWood","HitMetal","HitWeapon","HitStone"};
    return names.contains(a->Class->Name) && a->GetByte("ParticleType")==0 &&
        !a->GetBool("bEventSystemInit") && !a->GetBool("bEventSystemTick") && !a->GetBool("bEventParticleTick");
}
EntityKind kind(UActor* a)
{
    if (replicatedEffect(a)) return EntityKind::Effect;
    if (a->IsA("Pawn")) return EntityKind::Pawn;
    if (a->IsA("Carcass")) return EntityKind::Corpse;
    if (a->IsA("Debris") || a->IsA("Gib")) return EntityKind::Debris;
    if (a->IsA("Shield")) return EntityKind::Shield;
    if (a->IsA("LimbWeapon")) return EntityKind::Limb;
    if (a->IsA("Weapon")) return EntityKind::Weapon;
    if (a->IsA("Projectile")) return EntityKind::Projectile;
    if (GetMovingBrush(a)) return EntityKind::Mover;
    if (a->IsA("Pump")) return EntityKind::Objective;
    return EntityKind::Pickup;
}
UActor* object(UActor* a,const char* name)
{ return a->HasProperty(name)?UObject::TryCast<UActor>(a->GetUObject(name)):nullptr; }
ActorAppearance appearanceOf(UActor* actor,uint8_t first)
{
    ActorAppearance out;out.firstGroup=first;out.mesh=actor->GetByte("SkelMesh");
    if(auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));model && model->package) {
        out.skeleton={model->package->GetPackageName().ToString(),model->exportIndex+1};
        // Authored refuse (including EatenLegBone) can retain a variant index
        // beyond its display model. Match PrepareMesh's variant-zero fallback
        // before encoding, without changing the authoritative actor.
        if(out.mesh>=model->Meshes.size())out.mesh=0;
    }
    if(auto* substitute=UObject::TryCast<USkelModel>(actor->GetUObject("SubstituteMesh"));substitute && substitute->package) {
        out.substitute={substitute->package->GetPackageName().ToString(),substitute->exportIndex+1};
        out.mesh=actor->GetByte("SkelMesh");if(out.mesh>=substitute->Meshes.size())out.mesh=0;
    }
    out.scale=int16_t(std::lround(std::clamp(actor->DrawScale(),-16.f,16.f)*1024));out.drawType=uint8_t(actor->DrawType());
    auto* groups=static_cast<int32_t*>(actor->GetProperty("SkelGroupFlags"));
    auto* joints=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
    auto** skins=static_cast<UObject**>(actor->GetProperty("SkelGroupSkins"));
    std::copy_n(groups,16,out.groupFlags.begin());std::copy_n(joints,50,out.jointFlags.begin());
    for(size_t i=0;i<8;++i)if(auto* skin=skins[first+i];skin && skin->package)
        out.skins[i]={skin->package->GetPackageName().ToString(),skin->exportIndex+1};
    return out;
}
uint16_t animation(UActor* a)
{
    if (!a) return 0;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));
    if (model) for (size_t i=0;i<model->Sequences.size() && i<UINT16_MAX;++i)
        if (model->Sequences[i].Info.Name==a->AnimSequence()) return uint16_t(i+1);
    return 0;
}
bool validAnimation(UActor* a,uint16_t index)
{
    if (!index) return true;
    auto* model=a?UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a)):nullptr;
    return model && index<=model->Sequences.size();
}
void setAnimation(UActor* a,uint16_t index,int16_t phase,int16_t rate,bool looping)
{
    if (!a || !index) return;
    auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(a));
    auto sequence=model->Sequences[index-1].Info.Name;
    if(phase<0 && (a->AnimSequence()!=sequence || a->AnimFrame()>=0))
    { model->CaptureTween(a);a->RuneTweenTarget=sequence; }
    a->AnimSequence()=sequence;
    a->AnimFrame()=float(phase)/32767.0f;a->AnimRate()=float(rate)/256.0f;
    a->bAnimLoop()=looping;
}
const Entity* findEntity(const World& world,EntityRef ref)
{
    auto it=std::lower_bound(world.begin(),world.end(),ref.id,
        [](const Entity& e,uint32_t id){return e.ref.id<id;});
    return it!=world.end() && it->ref.id==ref.id ? &*it : nullptr;
}
}
MatchContract MakeRuneMatchContract(Engine& game,const std::string& map,uint16_t maxPlayers,GameMode mode,uint8_t teams,uint8_t arenaTeamSize,bool limbsScore)
{
    namespace fs=std::filesystem;
    std::vector<fs::path> scripts;
    for(const auto& entry:fs::directory_iterator(game.packages->GetSystemFolderPath()))
        if(entry.is_regular_file() && entry.path().extension()==".u")scripts.push_back(entry.path());
    std::sort(scripts.begin(),scripts.end(),[](const fs::path& a,const fs::path& b){return a.filename()<b.filename();});
    auto* levelPackage=game.packages->GetPackage(map);
    if(!levelPackage)throw std::runtime_error("missing match map package");
    MatchContract contract;contract.maxPlayers=maxPlayers;contract.map=map;
    contract.mode=mode;contract.teams=teams;contract.arenaTeamSize=arenaTeamSize;contract.limbsScore=limbsScore;contract.rulesRevision=OpenRuneBuild::GameplayRulesRevision; // Bump when native combat or movement rules change.
    contract.gameplayHash=hashFiles(scripts);
    // Bind the canonical selected laws into the existing authenticated content contract.
    std::array<uint8_t,40> rules{};
    std::copy(contract.gameplayHash.begin(),contract.gameplayHash.end(),rules.begin());
    auto mask=RuneBattleLaws::Mask();
    rules[32]='L';rules[33]='A';rules[34]='W';rules[35]=1;
    for(unsigned i=0;i<4;++i)rules[36+i]=uint8_t(mask>>(i*8));
    if(mbedtls_sha256(rules.data(),rules.size(),contract.gameplayHash.data(),0))throw std::runtime_error("laws hash failed");
    contract.mapHash=hashFiles({levelPackage->GetPackageFilePath()});
    return contract;
}
RuneNetworkAuthority::RuneNetworkAuthority(Engine& g):game(g)
{
    if (game.NetworkCombatAuthority) throw std::runtime_error("network authority already attached");
    game.NetworkCombatAuthority=this;
}
RuneNetworkAuthority::~RuneNetworkAuthority()
{
    if (game.NetworkCombatAuthority==this) game.NetworkCombatAuthority=nullptr;
    std::cout<<"OPEN_RUNE_MELEE_WINDOW committed="<<contactsCommitted<<" late_blocks="<<lateBlocks<<" cancelled="<<contactsCancelled<<" pending="<<contacts.size()<<" mean_wait_ms="<<(contactsCommitted?double(contactWaitTotal)/contactsCommitted:0)<<" max_wait_ms="<<contactWaitMax<<std::endl;
}
void RuneNetworkAuthority::recordSound(UActor* actor,int channel,USound* sound,vec3 at,float volume,float radius,float pitch)
{
    if(!actor || !sound || !sound->package || actor->Index<0 || actor->Index>=0x100000 || sounds.size()>=128 ||
        !std::isfinite(volume) || !std::isfinite(radius) || !std::isfinite(pitch))return;
    SoundCue cue;cue.source=identity(actor);cue.emitter=actor->Index;cue.object=sound->exportIndex+1;
    cue.package=sound->package->GetPackageName().ToString();cue.channel=channel&15;
    for(int i=0;i<3;++i){if(!std::isfinite(at[i]) || std::abs(at[i])>16777216)return;cue.position[i]=int32_t(std::lround(at[i]*8));}
    cue.volume=uint16_t(std::lround(std::clamp(volume,0.f,8.f)*256));
    cue.radius=uint16_t(std::lround(std::clamp(radius>0?radius:1500.f,1.f,65535.f)));
    cue.pitch=uint16_t(std::lround(std::clamp(pitch,.125f,4.f)*256));sounds.push_back(std::move(cue));
}
std::vector<SoundCue> RuneNetworkAuthority::drainSounds() { auto out=std::move(sounds);sounds.clear();return out; }
void RuneNetworkAuthority::beginTick(uint64_t tick,uint64_t now)
{
    if (!tick || tick<simulationTick) { combatFault=true; return; }
    simulationTick=tick;wallTime=now?now:tick*1000/60;
    synchronizeLives();
    for(auto& [id,player]:players)
    {
        // UPlayerPawn runs PlayerTick after actor physics. The acceleration
        // consumed this tick therefore belongs to the previous input, not the
        // command about to be admitted. Receipt alone is not a movement ACK.
        player.heldTicks=player.pendingInput && player.pendingInput==player.physicsInput?
            std::min<uint16_t>(255,player.heldTicks+1):player.pendingInput?1:0;
        player.physicsInput=player.pendingInput;
    }
}
void RuneNetworkAuthority::synchronizeLives()
{
    for(auto& [id,player]:players) {
        auto* p=player.pawn;
        if(p->Health()<=0 && !player.diedAt)player.diedAt=simulationTick;
        else if(p->Health()>0 && player.diedAt) {
            if(player.life==UINT32_MAX){combatFault=true;continue;}
            ++player.life;player.diedAt=0;
            p->finisherSwingStart=p->finisherContact=p->finisherMissUntil=p->finisherPoseStart=-1;
            p->finisherHands=0;p->finisherCut=false;p->finisherAimTime=-1;p->finisherHeld=false;p->finisherWithdraw=-1;
            defenses.erase(p);pendingCombatActions.erase(p);
            p->Dual={};p->Bloodlust={};p->Slam={};player.lastJump=0;
            player.pendingInput=player.physicsInput=player.heldTicks=player.guardInput=0;
            p->bFire()=p->bAltFire()=p->bDuck()=0;p->aForward()=p->aStrafe()=p->aUp()=0;
            std::cout<<"OPEN_RUNE_PLAYER_LIFE player="<<id<<" life="<<player.life<<" health="<<p->Health()<<std::endl;
        }
    }
}
uint32_t RuneNetworkAuthority::playerLife(uint64_t peer) const
{auto it=players.find(peer);return it==players.end()?0:it->second.life;}
std::optional<MovementAck> RuneNetworkAuthority::movementAck(uint64_t peer) const
{
    auto it=players.find(peer);if(it==players.end())return {};
    const auto& player=it->second;auto* p=player.pawn;if(RunePhysics::Controls(p) || RuneSpider::Busy(p))return {};
    MovementAck a;a.ref=identity(p);a.life=player.life;a.input=player.physicsInput;a.heldTicks=player.heldTicks;
    auto* zone=p->Region().Zone;
    float factor=(p->bIsWalking()?.3f:1.0f)*RuneCorpse::SpeedScale(p)*RuneLeg::SpeedScale(p);
    auto quantize=[](float v,float scale)->uint16_t {
        return std::isfinite(v) && v>0 && v*scale<=65535?uint16_t(std::round(v*scale)):0;
    };
    a.speed=quantize(p->GroundSpeed()*factor,8);a.acceleration=quantize(p->AccelRate()*factor,1);
    a.friction=zone?quantize(zone->ZoneGroundFriction(),256):0;a.step=quantize(p->MaxStepHeight(),8);
    a.mode=a.input && a.heldTicks && p->Health()>0 && p->Physics()==PHYS_Walking &&
        p->GetStateName()=="PlayerWalking" && (!p->ActorBase() || p->ActorBase()==game.LevelInfo || p->ActorBase()->bStatic()) && !p->LadderMode && !p->GetBool("bIsCrouching") &&
        !p->RuneKickActive && !p->RuneBashPhase && !RuneBloodlust::Busy(p) && p->RuneKickRecoilTime<=0 && zone &&
        !zone->bWaterZone() && zone->ZoneGravity().z<0 && length(zone->ZoneVelocity())<.001f &&
        a.speed && a.acceleration && a.step && a.friction<=25600;
    if(!a.mode && a.input && a.heldTicks && p->Health()>0 && p->Physics()==PHYS_Falling &&
        (!p->ActorBase() || p->ActorBase()==game.LevelInfo || p->ActorBase()->bStatic()) &&
        p->GetStateName()=="PlayerWalking" && !p->LadderMode && !p->GetBool("bIsCrouching") &&
        !p->RuneKickActive && !p->RuneBashPhase && !RuneBloodlust::Busy(p) && p->RuneKickRecoilTime<=0 &&
        zone && !zone->bWaterZone() && p->FootRegion().Zone && !p->FootRegion().Zone->bWaterZone() &&
        zone->ZoneGravity().z<0 && std::abs(zone->ZoneGravity().x)<.001f && std::abs(zone->ZoneGravity().y)<.001f &&
        length(zone->ZoneVelocity())<.001f && std::isfinite(p->AirControl()) && p->AirControl()>=0) {
        a.speed=quantize(p->GroundSpeed()*RuneLeg::SpeedScale(p),8);
        a.acceleration=quantize(game.LaunchInfo.ue1Version>219?p->AirControl()*p->AccelRate():0,1);
        a.friction=quantize(-zone->ZoneGravity().z,1);a.step=quantize(zone->ZoneTerminalVelocity(),8);
        if(a.speed && a.friction && a.step && a.friction<=25600)a.mode=MovementFalling;
    }
    return a;
}
uint32_t RuneNetworkAuthority::life(UPawn* p) const
{
    for (auto& [peer,player]:players) { (void)peer; if (player.pawn==p) return player.life; }
    return identity(p).generation;
}
void RuneNetworkAuthority::prepareCombat(UPawn* attacker,UPawn* target)
{
    if (!attacker || !target || !track(attacker) || !track(target)) combatFault=true;
}
void RuneNetworkAuthority::recordCombat(UPawn* attacker,UPawn* target,bool bash,bool blocked,bool staggered)
{
    // Damage scripts can mark a target deleted before this post-impact callback.
    // A reference reserved by prepareCombat remains valid for the result.
    auto knownOrTrack=[&](UPawn* p){return identity(p).id || track(p);};
    if (!game.NetworkAuthority || !simulationTick || combatFault || !attacker ||
        !knownOrTrack(attacker) || (target && !knownOrTrack(target)) || combat.size()>=64 || nextCombatId==UINT64_MAX)
    { combatFault=true; return; }
    auto action=pendingCombatActions.find(attacker);
    uint64_t actionId=0;
    if(action!=pendingCombatActions.end() && action->second.move==(bash?CombatMove::Bash:CombatMove::Kick))
    { actionId=action->second.id;pendingCombatActions.erase(action); }
    int health=target?target->Health():0;
    if (health<INT16_MIN || health>INT16_MAX) { combatFault=true; return; }
    CombatOutcome result{nextCombatId,simulationTick,actionId,identity(attacker),identity(target),
        life(attacker),target?life(target):0,bash?CombatMove::Bash:CombatMove::Kick,
        target?(blocked?CombatContact::Blocked:CombatContact::Hit):CombatContact::Miss,
        int16_t(health),staggered};
    Bytes bytes;
    if (!encodeCombatOutcome(1,result,bytes)) { combatFault=true; return; }
    ++nextCombatId; combat.push_back(result);
}
void RuneNetworkAuthority::recordWeapon(UPawn* attacker,UPawn* target,bool blocked)
{
    if(!attacker || !target || attacker==target || !simulationTick)return;
    if(!identity(attacker).id || !identity(target).id || combat.size()>=64 || nextCombatId==UINT64_MAX) {combatFault=true;return;}
    CombatOutcome outcome{nextCombatId,simulationTick,0,identity(attacker),identity(target),life(attacker),life(target),CombatMove::Weapon,
        blocked?CombatContact::Blocked:CombatContact::Hit,int16_t(std::clamp(target->Health(),int(INT16_MIN),int(INT16_MAX))),false};
    Bytes bytes;if(!encodeCombatOutcome(1,outcome,bytes)){combatFault=true;return;}
    ++nextCombatId;combat.push_back(outcome);
}
void RuneNetworkAuthority::cancelCombat(UPawn* attacker,CombatMove move)
{
    auto it=pendingCombatActions.find(attacker);
    if(it==pendingCombatActions.end() || it->second.move!=move)return;
    if(cancelledActions.size()>=64){combatFault=true;return;}
    cancelledActions.push_back({it->second.peer,it->second.id});pendingCombatActions.erase(it);
}
std::vector<RuneNetworkAuthority::CancelledAction> RuneNetworkAuthority::drainCancelledActions()
{
    auto out=std::move(cancelledActions);cancelledActions.clear();return out;
}
bool RuneNetworkAuthority::releaseCombatThrough(uint64_t id)
{
    auto it=std::find_if(combat.begin(),combat.end(),[&](const CombatOutcome& c){return c.id==id;});
    if(it==combat.end())return false;
    combat.erase(combat.begin(),std::next(it)); return true;
}
bool RuneNetworkAuthority::releaseActorsThrough(uint64_t id)
{
    auto it=std::find_if(actorEvents.begin(),actorEvents.end(),[&](const ActorEvent& e){return e.id==id;});
    if(it==actorEvents.end())return false;
    actorEvents.erase(actorEvents.begin(),std::next(it));return true;
}
bool RuneNetworkAuthority::track(UActor* a)
{
    if (!a || a->bDeleteMe() || a->XLevel()!=game.Level) return false;
    if(identity(a).id)return true;
    if(actorEvents.size()>=1024 || nextActorEventId==UINT64_MAX || !a->Class || !a->Class->package)
    { combatFault=true;return false; }
    auto ref=registry.bind(reinterpret_cast<uintptr_t>(a));
    if (!ref) return false;
    ActorEvent event{nextActorEventId,*ref,ActorOperation::Spawn,kind(a),
        a->Class->package->GetPackageName().ToString(),a->Class->Name.ToString()};
    if(auto* proxy=object(a,"AnimProxy"))
    {
        if(!proxy->Class || !proxy->Class->package){combatFault=true;registry.release(reinterpret_cast<uintptr_t>(a));return false;}
        event.proxyPackage=proxy->Class->package->GetPackageName().ToString();
        event.proxyClass=proxy->Class->Name.ToString();
    }
    if(event.kind==EntityKind::Mover || event.kind==EntityKind::Objective || (event.kind==EntityKind::Pickup&&RunePhysics::IsProp(a)&&!a->RuneSpawnedDuringPlay))event.mapActor=a->Name.ToString();
    Bytes bytes;
    if(!encodeActorEvent(1,event,bytes)){combatFault=true;registry.release(reinterpret_cast<uintptr_t>(a));return false;}
    actors[ref->id]=a;actorEvents.push_back(std::move(event));++nextActorEventId;return true;
}
EntityRef RuneNetworkAuthority::identity(UActor* a) const { return registry.find(reinterpret_cast<uintptr_t>(a)); }
std::vector<ActorEvent> RuneNetworkAuthority::actorBaseline() const
{
    std::vector<ActorEvent> out;
    for(const auto& [id,a]:actors)if(a && !a->bDeleteMe()) {
        ActorEvent event{uint64_t(out.size()+1),identity(a),ActorOperation::Spawn,kind(a),a->Class->package->GetPackageName().ToString(),a->Class->Name.ToString()};
        if(auto* proxy=object(a,"AnimProxy")) {event.proxyPackage=proxy->Class->package->GetPackageName().ToString();event.proxyClass=proxy->Class->Name.ToString();}
        if(event.kind==EntityKind::Mover || event.kind==EntityKind::Objective || (event.kind==EntityKind::Pickup&&RunePhysics::IsProp(a)&&!a->RuneSpawnedDuringPlay))event.mapActor=a->Name.ToString();
        out.push_back(std::move(event));
        if(UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a)))for(uint8_t first:{0,8}) {
            auto state=appearanceOf(a,first);
            // Spawn copies the same content-hashed class defaults on both ends.
            // Only instance differences need reliable appearance traffic.
            if(state==appearanceOf(a->Class->GetDefaultObject<UActor>(),first))continue;
            ActorEvent visual;visual.id=out.size()+1;visual.ref=identity(a);visual.operation=ActorOperation::Appearance;visual.kind=kind(a);visual.appearance=std::move(state);out.push_back(std::move(visual));
        }
    }
    return out;
}
bool RuneNetworkAuthority::bind(uint64_t peer,UPlayerPawn* pawn,uint32_t life)
{
    if (!peer || !life || !pawn || !game.NetworkAuthority || players.size()>=32 || players.contains(peer) || pawn->Health()<=0 || pawn->Role()!=ROLE_Authority || pawn->NetworkDriven) return false;
    for (auto& [id,p]:players) { (void)id; if(p.pawn==pawn) return false; }
    if (!track(pawn)) return false;
    pawn->NetworkDriven=true; players.emplace(peer,Player{pawn,life}); return true;
}
void RuneNetworkAuthority::unbind(uint64_t peer)
{
    auto it=players.find(peer); if (it==players.end()) return;
    auto* p=it->second.pawn;
    if (!p->bDeleteMe())
    {
        p->aForward()=p->aStrafe()=p->aUp()=p->aTurn()=p->aLookUp()=0;
        p->Acceleration()=vec3(0); p->bFire()=p->bAltFire()=p->bDuck()=0;
        CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    }
    RuneSpider::Stop(p);
    RuneCorpse::Drop(p);
    p->NetworkDriven=false; players.erase(it);defenses.erase(p);
    std::erase_if(contacts,[&](const auto& c){return c.attacker==p || c.target==p;});
    pendingCombatActions.erase(p);
}
bool RuneNetworkAuthority::input(uint64_t peer,const Input& i)
{
    auto it=players.find(peer); if (it==players.end()) return false;
    auto* p=it->second.pawn;
    if (p->bDeleteMe() || p->Health()<=0 || i.life!=it->second.life || i.forward<-1000 || i.forward>1000 ||
        i.strafe<-1000 || i.strafe>1000 || i.pitch<-16384 || i.pitch>16384 || (i.held&~7)) return false;
    if(RunePhysics::Controls(p)){p->bFire()=p->bAltFire()=p->bDuck()=0;p->aForward()=p->aStrafe()=p->aUp()=0;return true;}
    bool newInput=i.sequence!=it->second.pendingInput;
    it->second.pendingInput=i.sequence;
    double length=std::hypot(double(i.forward),double(i.strafe));
    double scale=length>1000 ? 1000.0/length : 1.0;
    RuneSpider::Input(p,float(i.forward*scale/1000),float(i.strafe*scale/1000));
    p->aForward()=float(i.forward*scale*6); p->aStrafe()=float(i.strafe*scale*6);
    p->aTurn()=p->aLookUp()=p->aBaseX()=p->aBaseY()=p->aMouseX()=p->aMouseY()=0;
    if(!(i.held&2) && (newInput || p->bAltFire()))
        defense(p,false,i.sequence?i.createdMs:wallTime,wallTime,i.yaw);
    p->bFire()=uint8_t(i.held&1); p->bDuck()=uint8_t((i.held>>2)&1);
    // Match the original Duck alias: hold bDuck on land and a negative aUp
    // while swimming. Network-driven pawns skip hardware PlayerInput, so both
    // halves must be reconstructed from the complete held state every tick.
    // PlayerInput scales the original -300 axis by 20 before PlayerMove.
    p->aUp()=(i.held&4)?-6000.0f:0.0f;
    // Guard actions and complete inputs use independent delivery lanes. A later
    // held-state command must not be undone by an older movement datagram.
    if(i.sequence>=it->second.guardInput){
        it->second.guardInput=i.sequence;p->bAltFire()=uint8_t((i.held>>1)&1);
        if(!(i.held&2))CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    }
    p->ViewRotation()=Rotator(int(i.pitch)&65535,i.yaw,0);
    // Rotation belongs to the authoritative movement path; client never sets location.
    // Complete block state repairs a lost release without synthesizing another bash.
    return true;
}
bool RuneNetworkAuthority::action(uint64_t peer,const AdmittedAction& a,uint64_t now)
{
    auto it=players.find(peer); if (it==players.end() || now<a.localTime || now-a.localTime>InputMaxAge || a.action.life!=it->second.life) return false;
    auto* p=it->second.pawn;
    auto& watermark=it->second;
    if(!a.action.id || a.action.id<=watermark.lastAction || a.localTime<watermark.lastActionTime)return false;
    watermark.lastAction=a.action.id;watermark.lastActionTime=a.localTime;
    if(a.action.kind==ActionKind::Respawn)
    {
        auto& player=it->second;
        if(p->bDeleteMe() || p->Health()>0 || p->GetStateName()!="Dying" || !player.diedAt ||
            simulationTick-player.diedAt<180 || player.life==UINT32_MAX ||
            (p->HasProperty("bCanRestart") && !p->GetBool("bCanRestart")))return false;
        if(!CallEvent(game.GameInfo,"RestartPlayer",{ExpressionValue::ObjectValue(p)}).ToBool())return false;
        synchronizeLives();
        p->bFire()=p->bAltFire()=p->bDuck()=0;p->aForward()=p->aStrafe()=p->aUp()=0;
        CallEvent(game.GameInfo,"StartPlayer",{ExpressionValue::ObjectValue(p)});
        CallEvent(p,"ClientReStart");CallEvent(p,"PlayerRestart");
        return p->Health()>0;
    }
    if (p->bDeleteMe() || p->Health()<=0 || p->GetStateName()=="Dying" || p->GetStateName()=="Unresponsive") return false;
    if(RuneSpider::Busy(p)){
        if(a.action.kind==ActionKind::Use){RuneSpider::Stop(p);return true;}
        if(a.action.kind==ActionKind::Jump || a.action.kind==ActionKind::BloodLeap){RuneSpider::Stop(p,true);watermark.lastJump=0;return true;}
        return a.action.kind==ActionKind::LowerShield;
    }
    if(RuneBloodlust::Busy(p) && a.action.kind!=ActionKind::LowerShield && !(a.action.kind==ActionKind::Attack && RuneSlam::CanStart(p)))return false;
    if(RunePhysics::Controls(p))return false;
    if(RunePhysics::Carrying(p)){if(a.action.kind==ActionKind::Attack||a.action.kind==ActionKind::Throw)return RunePhysics::ThrowProp(p);if(a.action.kind==ActionKind::Use)return RunePhysics::UseProp(p);return false;}
    if(RuneCorpse::Busy(p)) {
        if(a.action.kind==ActionKind::Attack || a.action.kind==ActionKind::Throw)return RuneCorpse::Throw(p);
        if(a.action.kind==ActionKind::Use){RuneCorpse::Drop(p);return true;}
        return a.action.kind==ActionKind::LowerShield;
    }
    if(p->finisherHeld){
        if(a.action.kind==ActionKind::Attack)return RunePhysics::ReleaseImpale(p);
        return a.action.kind==ActionKind::LowerShield;
    }
    switch(a.action.kind)
    {
    case ActionKind::Attack:
        if (!object(p,"Weapon")) return false;
        CallEvent(p,"Fire",{ExpressionValue::FloatValue(0)}); return true;
    case ActionKind::RaiseShield:
        if (!object(p,"Shield") && !RuneDualWield::Equipped(p)) return false;
        if(a.input.sequence<watermark.guardInput){
            // Preserve historical on-time defense for the contact window while
            // leaving the newer live press/release state untouched.
            auto* shield=object(p,"Shield");auto* proxy=object(p,"AnimProxy");
            if((!proxy || proxy->GetStateName()=="Idle" || proxy->GetStateName()=="Defending") &&
                (RuneDualWield::Equipped(p) || (shield && shield->GetInt("Health")>0 && shield->GetUObject("AttachParent")==p)))
                defense(p,true,a.localTime,now,a.input.yaw);
            return true;
        }
        watermark.guardInput=a.input.sequence;p->bAltFire()=1;
        CallEvent(p,"AltFire",{ExpressionValue::FloatValue(0)});
        if(auto* shield=object(p,"Shield");p->Dual.guard ||
            (!RuneDualWield::Equipped(p) && shield && shield->GetStateName()=="Active" && shield->GetInt("Health")>0))
            defense(p,true,a.localTime,now,a.input.yaw);
        return true;
    case ActionKind::LowerShield:
        defense(p,false,a.localTime,now,a.input.yaw);
        if(a.input.sequence>=watermark.guardInput){
            watermark.guardInput=a.input.sequence;p->bAltFire()=0;
            CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
        }
        return true;
    case ActionKind::Kick:
        if (!RuneKick::Start(p)) return false;
        pendingCombatActions[p]={peer,a.action.id,CombatMove::Kick}; return true;
    case ActionKind::Bash:
        if (!RuneBash::Start(p)) return false;
        pendingCombatActions[p]={peer,a.action.id,CombatMove::Bash}; return true;
    case ActionKind::SelectFists: case ActionKind::SelectSword: case ActionKind::SelectHammer: case ActionKind::SelectAxe: {
        auto* proxy=object(p,"AnimProxy");
        if(!proxy || proxy->GetStateName()!="Idle" || p->GetStateName()!="PlayerWalking")return false;
        CallEvent(p,"SwitchWeapon",{ExpressionValue::ByteValue(uint8_t(a.action.kind)-uint8_t(ActionKind::SelectFists)+1)});
        return true;
    }
    case ActionKind::Use: CallEvent(p,"Use"); return true;
    case ActionKind::Powerup: {
        auto* weapon=object(p,"Weapon");
        if(!weapon || !weapon->GetBool("bCanBePoweredUp") || weapon->GetBool("bPoweredUp"))return false;
        int before=p->GetInt("RunePower"), cost=weapon->GetInt("RunePowerRequired");
        if(cost<=0 || before<cost)return false;
        CallEvent(p,"Powerup");
        return weapon->GetBool("bPoweredUp") && p->GetInt("RunePower")==before-cost;
    }
    case ActionKind::Throw:
        if (!object(p,"Weapon")) return false;
        CallEvent(p,"Throw"); return true;
    case ActionKind::Jump:
        if(p->GetStateName()=="EdgeHanging") {
            // The original ledge state owns pull-up/drop collision. Do not
            // treat this as a new airborne jump or arm a giant-leap combo.
            watermark.lastJump=0;p->Bloodlust.tap=-1;
            CallEvent(p,"Jump",{ExpressionValue::FloatValue(0)});
            auto* climbProxy=UObject::TryCast<UActor>(object(p,"AnimProxy"));
            if(p->GetStateName()=="EdgeHanging" && p->AnimRate()==0 && p->AnimFrame()>=0 &&
               (!climbProxy || climbProxy->AnimRate()==0)) {
                // A finished/stalled automatic pull-up cannot leave a living
                // pawn flying forever. Release at the current collision-valid
                // position; ordinary swept falling owns the descent.
                p->GotoState("PlayerWalking","Begin");p->SetPhysics(PHYS_Falling);
                p->SetBase(nullptr,true);p->Velocity().z=std::min(0.f,p->Velocity().z);
                LogMessage("RUNE_LEDGE_RELEASE actor="+p->Name.ToString());
            }
            return true;
        }
        if(p->Physics()!=PHYS_Walking || p->GetStateName()!="PlayerWalking")return false;
        watermark.lastJump=a.localTime;p->Bloodlust.tap=game.LevelInfo->TimeSeconds();
        CallEvent(p,"Jump",{ExpressionValue::FloatValue(0)});return true;
    case ActionKind::BloodLeap: {
        // Gesture timing belongs to authenticated input time, not packet arrival.
        if(!watermark.lastJump || a.localTime<watermark.lastJump || a.localTime-watermark.lastJump>320)return false;
        watermark.lastJump=0;
        auto previousTap=p->Bloodlust.tap;p->Bloodlust.tap=game.LevelInfo->TimeSeconds();
        float yaw=float(a.input.yaw)*(6.28318530718f/65536);
        vec3 direction=vec3(std::cos(yaw),std::sin(yaw),0)*float(a.input.forward)+
            vec3(-std::sin(yaw),std::cos(yaw),0)*float(a.input.strafe);
        bool armed=RuneSlam::Arm(p);
        bool accepted=RuneBloodlust::Leap(p,direction);
        if(!accepted)p->Bloodlust.tap=previousTap;
        return accepted || armed;
    }
    case ActionKind::ShieldCharge:
        if(!(a.input.held&2))return false;
        return RuneBloodlust::Charge(p);
    case ActionKind::Respawn: return false; // Handled before the living-player gate.
    }
    return false;
}
void RuneNetworkAuthority::defense(UPawn* p,bool raised,uint64_t pressed,uint64_t received,uint16_t yaw)
{
    if(pressed>received || received-pressed>InputMaxAge)return;
    auto& records=defenses[p];records.push_back({pressed,received,life(p),raised,yaw,RuneDualWield::Equipped(p)?nullptr:object(p,"Shield"),p->Weapon(),p->Dual.offhand});
    while(records.size()>32)records.pop_front();
}
bool RuneNetworkAuthority::deferContact(UPawn* attacker,UPawn* target,bool blocked,UActor* weapon,std::function<void(bool)> apply,bool bash,bool charge)
{
    if(committingContact || !game.NetworkAuthority || !simulationTick || !attacker || !target)return false;
    if(game.GameInfo->IsA("HeadBallGame") && !game.GameInfo->GetBool("bGameStarted")) {
        if(!weapon && !charge)recordCombat(attacker,nullptr,bash,false,false);return true;
    }
    // Original team damage scaling does not cover our native kick momentum or
    // bash stagger. Suppress the complete contact when friendly fire is disabled.
    if(((game.GameInfo->IsA("TeamGame") && game.GameInfo->GetFloat("FriendlyFireScale")==0) || game.GameInfo->IsA("ArenaGameInfo")) && attacker!=target) {
        auto* a=attacker->GetUObject("PlayerReplicationInfo");auto* b=target->GetUObject("PlayerReplicationInfo");
        if(a && b && (a->GetByte("Team")<2 || game.GameInfo->IsA("ArenaGameInfo")) && a->GetByte("Team")==b->GetByte("Team")) {
            if(!weapon && !charge)recordCombat(attacker,nullptr,bash,false,false);
            return true;
        }
    }
    prepareCombat(attacker,target);
    if(combatFault || contacts.size()>=128){combatFault=true;return true;}
    contacts.push_back({attacker,target,weapon,RuneDualWield::Equipped(target)?nullptr:object(target,"Shield"),identity(attacker),identity(target),
        life(attacker),life(target),target->Weapon(),target->Dual.offhand,wallTime,simulationTick,blocked,bash,charge,RuneDualWield::Blocks(target,attacker),uint16_t(target->Rotation().Yaw),attacker->Location(),target->Location(),std::move(apply)});
    return true;
}
void RuneNetworkAuthority::commitContacts()
{
    while(!contacts.empty() && MeleeTiming::ready(contacts.front().at,contacts.front().tick,wallTime,simulationTick))
    {
        auto c=std::move(contacts.front());contacts.pop_front(); // Commitment watermark precedes VM side effects.
        auto* a=c.attacker;auto* t=c.target;
        bool sameShield=c.shield && object(t,"Shield")==c.shield && !c.shield->bDeleteMe() &&
            !c.shield->bHidden() && c.shield->Owner()==t && c.shield->GetInt("Health")>0 &&
            c.shield->GetUObject("AttachParent")==t && !RuneDualWield::Equipped(t);
        bool valid=identity(a)==c.attackerRef && identity(t)==c.targetRef && !a->bDeleteMe() && !t->bDeleteMe() &&
            life(a)==c.attackerLife && life(t)==c.targetLife && a->Health()>0 && t->Health()>0 &&
            (!c.blocked || sameShield) &&
            (c.weapon || c.bash || c.charge || t->RuneKickImmunity<=0) &&
            (!c.weapon || (!c.weapon->bDeleteMe() && c.weapon->Owner()==a && (object(a,"Weapon")==c.weapon || (RuneDualWield::Equipped(a) && a->Dual.offhand==c.weapon))));
        // Recheck both the frozen contact line and current cover, including movers.
        auto clear=[&](vec3 from,vec3 to){vec3 hit,normal;return !a->Trace(hit,normal,to+vec3(0,0,10),from+vec3(0,0,10),false,vec3(0));};
        valid=valid && clear(c.from,c.to) && clear(a->Location(),t->Location());
        if(!valid){
            if(!c.weapon && !c.charge)if(auto pending=pendingCombatActions.find(a);pending!=pendingCombatActions.end())cancelCombat(a,pending->second.move);
            ++contactsCancelled;continue;
        }
        bool blocked=c.blocked;
        auto found=defenses.find(t);
        if(!blocked && sameShield && found!=defenses.end())
        {
            const Defense* state=nullptr;
            for(const auto& d:found->second)if(d.life==c.targetLife && d.pressed<=c.at && (!state || d.pressed>=state->pressed))state=&d;
            if(state && state->raised && state->shield==c.shield && MeleeTiming::lateBlock(state->pressed,state->received,c.at)) {
                auto delta=c.from-c.to;delta.z=0;
                float yaw=float(state->yaw)*(6.28318530718f/65536);
                float contactYaw=float(c.targetYaw)*(6.28318530718f/65536);
                blocked=length(delta)>.01f && dot(vec3(std::cos(yaw),std::sin(yaw),0),normalize(delta))>=.25f &&
                    dot(vec3(std::cos(contactYaw),std::sin(contactYaw),0),normalize(delta))>=.25f;
                if(blocked)++lateBlocks;
            }
        }
        bool samePair=RuneDualWield::Equipped(t) && t->Weapon()==c.guardWeapon && t->Dual.offhand==c.guardOffhand;
        bool crossed=c.crossed && samePair;
        if(!crossed && !blocked && samePair && found!=defenses.end()) {
            const Defense* state=nullptr;
            for(const auto& d:found->second)if(d.life==c.targetLife && d.pressed<=c.at && (!state || d.pressed>=state->pressed))state=&d;
            if(state && state->raised && state->weapon==c.guardWeapon && state->offhand==c.guardOffhand &&
                c.at-state->pressed>=96 && MeleeTiming::lateBlock(state->pressed,state->received,c.at)) {
                auto delta=c.from-c.to;delta.z=0;
                float yaw=float(state->yaw)*(6.28318530718f/65536),atYaw=float(c.targetYaw)*(6.28318530718f/65536);
                crossed=length(delta)>.01f && dot(vec3(std::cos(yaw),std::sin(yaw),0),normalize(delta))>=.5f &&
                    dot(vec3(std::cos(atYaw),std::sin(atYaw),0),normalize(delta))>=.5f;
                if(crossed)++lateBlocks;
            }
        }
        struct GuardScope {
            RuneDualWield::State& s;
            GuardScope(RuneDualWield::State& state,bool crossed):s(state){s.contactGuardOverride=true;s.contactGuard=crossed;}
            ~GuardScope(){s.contactGuardOverride=false;s.contactGuard=false;}
        } guard(t->Dual,crossed);
        committingContact=true;
        try { c.apply(blocked); } catch(...) {committingContact=false;throw;}
        committingContact=false;++contactsCommitted;contactWaitTotal+=wallTime-c.at;contactWaitMax=std::max(contactWaitMax,wallTime-c.at);
    }
}
bool RuneNetworkAuthority::deferWeaponCall(UObject* instance,const NameString& name,const ArrayView<ExpressionValue>& args)
{
    if(committingContact || !simulationTick || (name!="DoWeaponSwipe" && name!="SpawnHitEffect"))return false;
    auto* weapon=UObject::TryCast<UActor>(instance);
    if(!weapon || !weapon->IsA("Weapon") || weapon->GetStateName()!="Swinging")return false;
    auto* attacker=UObject::TryCast<UPawn>(weapon->Owner());
    if(!attacker)return false;
    bool swipe=name=="DoWeaponSwipe";
    if(args.size()!=(swipe?6:5))return false;
    auto* struck=UObject::TryCast<UActor>(args[swipe?0:4].ToObject());
    auto* target=UObject::TryCast<UPawn>(struck);
    if(!target && struck && (struck->IsA("Shield") || struck->IsA("Weapon")))target=UObject::TryCast<UPawn>(struck->Owner());
    if(!target || target==attacker)return false;
    // The original sweep and SwipeArrayCheck already selected this contact.
    // Queue the damage plus its effect once; never repeat the collision query
    // at a later pose or retain VM stack references.
    if(!swipe)return true;
    attacker->finisherContact=game.TotalTime;
    int low=args[1].ToInt(),high=args[2].ToInt();vec3 location=args[3].ToVector(),normal=args[4].ToVector(),momentum=args[5].ToVector();
    auto* shield=object(target,"Shield");bool hitShield=struck==shield;
    // The same frontal shield rule applies before and during the grace window;
    // deliberately delivering a block late must not grant extra shield coverage.
    bool blocked=hitShield || (struck==target && RuneKick::ShieldBlocks(target,attacker));
    auto opening=RuneFinisher::Opening(attacker,target);
    auto legContact=RuneLeg::Exposure(weapon,target,uint32_t(low),uint32_t(high),location);
    auto* followWeapon=attacker->Dual.followAvailable && !attacker->Dual.left && attacker->Dual.offhand==weapon?attacker->Dual.offhand:nullptr;
    return deferContact(attacker,target,blocked,weapon,[weapon,struck,shield,low,high,location,normal,momentum,hitShield,legContact,followWeapon,opening,attacker,target](bool defended){
        RuneDualWield::FollowupDamageScope followupDamage(followWeapon);
        RuneFinisher::ReplayScope finisher(weapon,target,opening,defended || RuneDualWield::Blocks(target,attacker));
        RuneLeg::ReplayScope legReplay(defended?RuneLeg::Contact{}:legContact);
        auto* hit=defended&&!hitShield?shield:struck;
        if(!hit || hit->bDeleteMe())return;
        int lo=defended&&!hitShield?0:low,hi=defended&&!hitShield?0:high;
        CallEvent(weapon,"DoWeaponSwipe",{ExpressionValue::ObjectValue(hit),ExpressionValue::IntValue(lo),ExpressionValue::IntValue(hi),
            ExpressionValue::VectorValue(location),ExpressionValue::VectorValue(normal),ExpressionValue::VectorValue(momentum)});
        CallEvent(weapon,"SpawnHitEffect",{ExpressionValue::VectorValue(location),ExpressionValue::VectorValue(normal),
            ExpressionValue::IntValue(lo),ExpressionValue::IntValue(hi),ExpressionValue::ObjectValue(hit)});
    });
}
bool RuneNetworkAuthority::capture(World& out)
{
    auto fault=[&](const char* reason,UActor* a=nullptr) {
        std::cout<<"OPEN_RUNE_CAPTURE_FAULT reason="<<reason<<" actor="<<(a?a->Name.ToString():"none")<<" tracked="<<actors.size()<<" queued_events="<<actorEvents.size()<<std::endl;return false;
    };
    synchronizeLives();
    for (auto it=actors.begin();it!=actors.end();)
    {
        if (it->second->bDeleteMe()) {
            if(actorEvents.size()>=1024 || nextActorEventId==UINT64_MAX)return fault("actor retention limit");
            ActorEvent event{nextActorEventId,identity(it->second),ActorOperation::Despawn,kind(it->second),{}, {}};
            actorEvents.push_back(event);++nextActorEventId;
            appearances.erase({it->first,0});appearances.erase({it->first,8});
            registry.release(reinterpret_cast<uintptr_t>(it->second)); it=actors.erase(it);
        }
        else ++it;
    }
    // Replicate loose inventory as well as equipped items, so a throw or pickup
    // has one authority. Static geometry stays local to the agreed map.
    for(auto* actor:game.Level->Actors)
        if(actor && (actor->IsA("Pawn") || actor->IsA("Inventory") || actor->IsA("Projectile") || actor->IsA("Carcass") || actor->IsA("Debris") || actor->IsA("Gib") || replicatedEffect(actor) || RunePhysics::IsProp(actor) || GetMovingBrush(actor) || actor->IsA("Pump")) &&
            !actor->bDeleteMe() && !actor->NetworkReplica && actor->Role()==ROLE_Authority &&
            (!actor->bStatic() || actor->IsA("Pump")) && (!actor->bHidden() || GetMovingBrush(actor) || actor->IsA("Pump")))
            {
                if(!identity(actor).id && (kind(actor)==EntityKind::Corpse || kind(actor)==EntityKind::Debris || kind(actor)==EntityKind::Effect)) {
                    auto type=kind(actor);auto count=std::count_if(actors.begin(),actors.end(),[&](const auto& entry){return kind(entry.second)==type;});
                    if(count>=(type==EntityKind::Debris?96:32))continue;
                }
                if(!track(actor))return fault("track world actor",actor);
            }
    for(auto [id,a]:actors)
        if(a && !a->bDeleteMe() && UObject::TryCast<UPawn>(a))
            for(auto name:{"Weapon","Shield"})if(auto* equipment=object(a,name))if(!track(equipment))return fault("track equipment",equipment);
    for(auto [id,a]:actors)if(auto* p=UObject::TryCast<UPawn>(a);p && RuneDualWield::Equipped(p))
        if(!track(p->Dual.offhand))return fault("track offhand",p->Dual.offhand);
    // Hidden stowed inventory still belongs to the authoritative owner.
    for(const auto& [peer,player]:players) {
        (void)peer;std::set<UActor*> visited;
        for(auto* item=object(player.pawn,"Inventory");item;item=object(item,"Inventory")) {
            if(visited.size()>=64 || !visited.insert(item).second)return fault("inventory cycle or limit",item);
            if(!track(item))return fault("track inventory",item);
        }
    }
    for(const auto& [id,a]:actors)if(UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a)))for(uint8_t first:{0,8}) {
        auto visual=appearanceOf(a,first);auto key=std::make_pair(id,first);
        auto prior=appearances.find(key);
        if(prior==appearances.end() && visual==appearanceOf(a->Class->GetDefaultObject<UActor>(),first)) {
            appearances[key]=std::move(visual);continue;
        }
        if(prior==appearances.end() || prior->second!=visual) {
            if(actorEvents.size()>=1024 || nextActorEventId==UINT64_MAX)return fault("actor retention limit");
            ActorEvent event;event.id=nextActorEventId++;event.ref=identity(a);event.kind=kind(a);event.operation=ActorOperation::Appearance;event.appearance=visual;
            Bytes encoded;if(!encodeActorEvent(1,event,encoded))return fault("appearance encoding",a);
            actorEvents.push_back(std::move(event));appearances[key]=std::move(visual);
        }
    }
    World world;
    for (auto [id,a]:actors)
    {
        Entity e; e.ref=identity(a); e.kind=kind(a); e.flags=a->Physics()==PHYS_Walking?2:0;
        if(a->bHidden())e.flags|=32;
        if(e.kind==EntityKind::Weapon) {
            if(a->GetBool("bPoweredUp"))e.flags|=128;
            // Bit 2 is kind-specific (guard on pawns, carried on corpses).
            if(a->GetStateName()=="Swinging" || a->GetUObject("Swipe"))e.flags|=4;
        }
        // Flag 4 is guard for pawns, and centred relaxed presentation for corpses.
        if(e.kind==EntityKind::Corpse && a->CorpseBody.active)e.flags|=4;
        auto* pawn=UObject::TryCast<UPawn>(a);
        int health=pawn?pawn->Health():(a->HasProperty("Health")?a->GetInt("Health"):0);
        if (health<INT16_MIN || health>INT16_MAX) return fault("health bounds",a);
        e.health=int16_t(health); e.life=pawn?life(pawn):1; if (pawn && health>0) {
            e.flags|=1;
            if(RuneBattleLaws::Allowed(RuneBattleLaws::Finishers) && !pawn->bHidden() && (pawn->RuneBashPhase==2 || pawn->finisherMissUntil>game.TotalTime))
                e.flags|=FinisherOpeningFlag;
        }
        // Carcass InitFor inherits the dying pawn's PrePivot; its authored
        // collision resize later changes Location and PrePivot together. The
        // presentation replica retains its class-default pivot (no local VM).
        // Express only corpse positions in that basis so existing clients draw
        // the same mesh origin without an artificial hover or resize drop.
        // Authoritative collision/physics and pawn prediction stay unchanged.
        if(e.kind==EntityKind::Corpse && (!std::isfinite(a->Velocity().x)||!std::isfinite(a->Velocity().y)||!std::isfinite(a->Velocity().z)))
            RunePhysics::RepairInvalidMotion(a);
        vec3 presentationLocation=a->Location();
        if(e.kind==EntityKind::Corpse)
            presentationLocation+=a->PrePivot()-a->Class->GetDefaultObject<UActor>()->PrePivot();
        presentationLocation=RuneCorpse::SnapshotLocation(a,presentationLocation);
        for (int axis=0;axis<3;++axis)
        {
            auto position=quantizePosition(presentationLocation[axis]);
            // Authored doors can move over 8192 units/sec in a transition.
            // Velocity is a bounded presentation hint; positions remain exact
            // and authoritative. Never reject an otherwise valid world for it.
            const auto speed=double(a->Velocity()[axis]);
            if(!std::isfinite(speed))return fault("nonfinite velocity",a);
            auto velocity=quantizeVelocity(std::clamp(speed,double(INT16_MIN)/4,double(INT16_MAX)/4));
            if (!position || !velocity) return fault("position or velocity bounds",a);
            e.position[axis]=*position; e.velocity[axis]=*velocity;
        }
        e.angles={uint16_t(a->Rotation().Pitch),uint16_t(a->Rotation().Yaw),uint16_t(a->Rotation().Roll)};
        auto* proxy=object(a,"AnimProxy");
        e.bodyAnimation=animation(a); e.torsoAnimation=animation(proxy);
        if (!std::isfinite(a->AnimFrame()) || (proxy && !std::isfinite(proxy->AnimFrame()))) return fault("animation frame",a);
        e.animationPhase=int16_t(std::lround(std::clamp(double(a->AnimFrame()),-1.0,1.0)*32767));
        auto rate=quantizeAnimationRate(a->AnimRate()); if(!rate) return fault("animation rate",a); e.animationRate=*rate;
        if(e.bodyAnimation && a->bAnimLoop())e.flags|=8;
        if(e.torsoAnimation && proxy)
        {
            e.torsoPhase=int16_t(std::lround(std::clamp(double(proxy->AnimFrame()),-1.0,1.0)*32767));
            auto torsoRate=quantizeAnimationRate(proxy->AnimRate());if(!torsoRate)return fault("torso animation rate",a);e.torsoRate=*torsoRate;
            if(proxy->bAnimLoop())e.flags|=16;
        }
        e.owner=identity(a->Owner()); e.weapon=identity(object(a,"Weapon")); e.shield=identity(object(a,"Shield"));
        if(a->bCarriedItem() && e.owner.id && object(a,"AttachParent")==a->Owner())
            e.attachmentJoint=a->GetByte("AttachParentJoint");
        if(proxy && proxy->GetStateName()=="Defending")e.flags|=4;
        if(pawn) {
            auto& c=e.combat;const auto& d=pawn->Dual;const auto& b=pawn->Bloodlust;
            bool dual=RuneDualWield::Equipped(pawn) && !RuneSpider::Busy(pawn);
            c.offhand=dual?identity(d.offhand):EntityRef{};
            c.flags=(RuneFinisher::PoseCode(pawn)<<6)|(dual && d.followAvailable && !d.left && !d.guard?32:0)|(dual && d.left?1:0)|(dual && d.guard?2:0)|(RuneBloodlust::Active(pawn)?4:0)|(b.phase && b.rushing?8:0)|(RuneSpider::Busy(pawn)?16:0);
            if(pawn->finisherHeld && ((c.flags>>6)==1 || (c.flags>>6)==3) && pawn->Health()>0){
                auto relative=pawn->finisherAimWorld-pawn->Location();
                auto& aim=c.impaleAim.emplace();
                for(int axis=0;axis<3;++axis)aim[axis]=int16_t(std::lround(std::clamp(relative[axis],-256.f,256.f)*8.f));
            }
            c.guard=dual?uint8_t(std::lround(std::clamp(d.guardBlend,0.f,1.f)*255)):0;
            c.meter=uint8_t(std::clamp(pawn->IsA("RunePlayer")?100.f*pawn->GetInt("Strength")/std::max(1,(int)pawn->GetInt("MaxStrength")):b.rage,0.f,100.f));
            c.phase=uint8_t(b.phase);c.age=b.phase?uint16_t(std::lround(std::clamp(b.age,0.f,10.f)*1000)):0;
            c.cooldown=uint16_t(std::ceil(std::clamp(b.cooldown,0.f,4.f)*1000));c.ready=uint8_t(RuneBloodlust::Readiness(pawn));
            if(RuneSlam::Busy(pawn)){c.phase=uint8_t(5+pawn->Slam.phase);c.age=uint16_t(std::clamp(pawn->Slam.age*1000.f,0.f,10000.f));c.ready=0;c.flags&=~8;}
            if(pawn->IsA("RunePlayer")) {
                int capacity=pawn->GetInt("MaxPower"), power=pawn->GetInt("RunePower");
                if(capacity<0 || capacity>200 || power<0 || power>capacity)return fault("power bounds",a);
                c.runeCapacity=uint8_t(capacity);c.runePower=uint8_t(power);
            }
        }
        RunePhysics::Capture(a,e.physical);
        world.push_back(e);
    }
    if (!validWorld(world)) {
        for(const auto& e:world)std::cout<<"OPEN_RUNE_CAPTURE_ENTITY id="<<e.ref.id<<" kind="<<int(e.kind)<<" flags="<<int(e.flags)<<" health="<<e.health<<" owner="<<e.owner.id<<" weapon="<<e.weapon.id<<" shield="<<e.shield.id<<" joint="<<int(e.attachmentJoint)<<" offhand="<<e.combat.offhand.id<<" native_flags="<<int(e.combat.flags)<<" phase="<<int(e.combat.phase)<<std::endl;
        return fault("world invariant");
    }
    out=std::move(world); return true;
}
bool RuneNetworkReplica::bind(EntityRef ref,UActor* actor)
{
    if (!ref.id || !ref.generation || !actor || actor->bDeleteMe() || actors.contains(ref.id) || actors.size()>=MaxEntities) return false;
    for (auto& [id,item]:actors) { (void)id; if(item.second==actor)return false; }
    if (actor->XLevel()) actor->XLevel()->Collision.RemoveFromCollision(actor);
    actor->NetworkReplica=true;
    // Replicas deliberately skip PreBeginPlay. Initialize its visual-only Rune
    // defaults here without invoking relevance, collision or gameplay scripts.
    // Class defaults alone have different polygon flags (including environment
    // groups without textures), which otherwise erase Ragnar's head and limbs.
    if(auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));model && !model->Meshes.empty()) {
        auto* geometry=GetRuneGeometry(actor);
        size_t mesh=actor->GetByte("SkelMesh");if(mesh>=geometry->Meshes.size())mesh=0;
        auto* groups=static_cast<int*>(actor->GetProperty("SkelGroupFlags"));
        auto* joints=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
        for(size_t i=0;i<16;++i)groups[i]=geometry->Meshes[mesh].GroupFlags[i];
        for(size_t i=0;i<50;++i)joints[i]=i<model->Joints.size()?uint8_t(model->Joints[i].Flags):0;
    }
    actor->Role()=ROLE_SimulatedProxy;
    if(!GetMovingBrush(actor) && !actor->IsA("Pump")) {actor->bCollideActors()=false;actor->bBlockActors()=false;actor->bBlockPlayers()=false;}
    else if(actor->XLevel())actor->XLevel()->Collision.AddToCollision(actor);
    actors.emplace(ref.id,std::make_pair(ref,actor)); return true;
}
UActor* RuneNetworkReplica::actor(EntityRef ref) const
{
    auto it=actors.find(ref.id);
    return it!=actors.end() && it->second.first==ref?it->second.second:nullptr;
}
UActor* RuneNetworkReplica::spawn(Engine& game,const ActorEvent& event)
{
    if(event.operation!=ActorOperation::Spawn || actors.size()>=MaxEntities || actors.contains(event.ref.id))return nullptr;
    auto make=[&](const std::string& package,const std::string& type,const std::string& name)->UActor*
    {
        auto* pkg=game.packages->GetPackage(package);
        auto* cls=pkg?pkg->GetClass(type):nullptr;
        if(!cls)return nullptr;
        auto* actor=UObject::TryCast<UActor>(game.packages->GetTransientPackage()->NewObject(name,cls,ObjectFlags::Transient));
        if(!actor)return nullptr;
        actor->XLevel()=game.Level;actor->Level()=game.LevelInfo;
        actor->NetworkReplica=true;actor->bHidden()=true;
        actor->Index=int(game.Level->Actors.size());game.Level->Actors.push_back(actor);
        return actor;
    };
    if(event.kind==EntityKind::Mover || event.kind==EntityKind::Objective || (event.kind==EntityKind::Pickup&&!event.mapActor.empty())) {
        for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && !a->NetworkReplica && kind(a)==event.kind &&
            a->Name==NameString(event.mapActor) && a->Class->Name==NameString(event.actorClass) &&
            a->Class->package->GetPackageName()==NameString(event.package))
            return bind(event.ref,a)?a:nullptr;
        return nullptr;
    }
    auto suffix=std::to_string(event.ref.id)+"_"+std::to_string(event.ref.generation);
    auto* result=make(event.package,event.actorClass,"RemoteActor"+suffix);
    if(!result || kind(result)!=event.kind || !bind(event.ref,result))return nullptr;
    if(!event.proxyClass.empty())
    {
        auto* proxy=make(event.proxyPackage,event.proxyClass,"RemoteAnimation"+suffix);
        if(!proxy){unbind(event.ref);return nullptr;}
        proxy->Owner()=result;result->SetObject("AnimProxy",proxy);
    }
    return result;
}
bool RuneNetworkReplica::appearance(Engine& game,const ActorEvent& event)
{
    auto* a=actor(event.ref);if(!a || event.operation!=ActorOperation::Appearance || kind(a)!=event.kind)return false;
    const auto& v=event.appearance;
    auto fail=[&](const char* reason){std::cout<<"NET_APPEARANCE_REJECT class="<<a->Class->Name.ToString()<<" mesh="<<int(v.mesh)<<" model="<<v.skeleton.package<<":"<<v.skeleton.object<<" reason="<<reason<<std::endl;return false;};
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));
    if(v.skeleton.object) {
        auto* pkg=game.packages->GetPackage(v.skeleton.package);
        if(!pkg || v.skeleton.object>uint32_t(pkg->GetExportCount()))return fail("model export");
        model=UObject::TryCast<USkelModel>(pkg->GetUObject(int(v.skeleton.object)));if(!model)return fail("model type");
    }
    USkelModel* substitute=nullptr;
    if(v.substitute.object) {
        auto* pkg=game.packages->GetPackage(v.substitute.package);
        if(!pkg || v.substitute.object>uint32_t(pkg->GetExportCount()))return fail("substitute export");
        substitute=UObject::TryCast<USkelModel>(pkg->GetUObject(int(v.substitute.object)));
        if(!substitute)return fail("substitute type");
    }
    auto* geometry=substitute?substitute:model;
    if(!model || !geometry || v.mesh>=geometry->Meshes.size() || (v.firstGroup!=0 && v.firstGroup!=8))return fail("mesh range");
    for(const auto& vertex:geometry->Meshes[v.mesh].Vertices)
        if(vertex.Joint[0]<0 || size_t(vertex.Joint[0])>=model->Joints.size() ||
            (vertex.Weight!=1 && (vertex.Joint[1]<0 || size_t(vertex.Joint[1])>=model->Joints.size())))return fail("substitute joints");
    std::array<UObject*,8> resolved{};
    for(size_t i=0;i<8;++i)if(v.skins[i].object) {
        auto* pkg=game.packages->GetPackage(v.skins[i].package);
        if(!pkg || v.skins[i].object>uint32_t(pkg->GetExportCount()))return fail("skin export");
        resolved[i]=pkg->GetUObject(int(v.skins[i].object));
        if(!resolved[i] || !resolved[i]->IsA("Texture"))return fail("skin type");
    }
    if(v.skeleton.object)a->SetObject("Skeletal",model);
    a->SetObject("SubstituteMesh",substitute);
    a->DrawScale()=float(v.scale)/1024;a->DrawType()=EDrawType(v.drawType);
    a->SetByte("SkelMesh",v.mesh);
    std::copy(v.groupFlags.begin(),v.groupFlags.end(),static_cast<int32_t*>(a->GetProperty("SkelGroupFlags")));
    std::copy(v.jointFlags.begin(),v.jointFlags.end(),static_cast<uint8_t*>(a->GetProperty("JointFlags")));
    auto** skins=static_cast<UObject**>(a->GetProperty("SkelGroupSkins"));
    std::copy(resolved.begin(),resolved.end(),skins+v.firstGroup);
    RuneLeg::ReplicaAppearance(a);
    return true;
}
bool RuneNetworkReplica::unbind(EntityRef ref)
{
    if(anticipation.ref==ref)clearAnticipation();
    auto it=actors.find(ref.id);
    if(it==actors.end() || it->second.first!=ref)return false;
    auto* actor=it->second.second;
    for(auto& [id,item]:actors)
        if(id!=ref.id && item.second->Owner()==actor)item.second->Owner()=nullptr;
    auto detach=[](UActor* a)
    {
        if(!a)return;
        if(a->XLevel())a->XLevel()->Collision.RemoveFromCollision(a);
        if(auto* level=a->XLevel();level && a->Index>=0 && size_t(a->Index)<level->Actors.size() &&
            level->Actors[a->Index]==a)level->Actors[a->Index]=nullptr;
        a->bHidden()=true;a->Owner()=nullptr;a->ChildActors.clear();a->bDeleteMe()=true;
    };
    auto* proxy=object(actor,"AnimProxy");
    if(proxy){actor->SetObject("AnimProxy",nullptr);detach(proxy);}
    RuneIK::Forget(actor);
    detach(actor);
    animationPlayback.erase(ref.id);effectAges.erase(ref.id);actors.erase(it);healthOverlays.erase(ref.id);adaptiveLives.erase(ref.id);
    retired.push_back(ref);if(retired.size()>MaxEntities)retired.pop_front();
    return true;
}
bool RuneNetworkReplica::referencesRetired(const Snapshot& snapshot) const
{
    for(const auto& e:snapshot.world)
        if(std::find(retired.begin(),retired.end(),e.ref)!=retired.end())return true;
    return false;
}
bool RuneNetworkReplica::install(const Snapshot& s)
{
    rejection.clear();
    auto reject=[&](std::string why){rejection=std::move(why);return false;};
    if (!s.id || s.id<=current.id || !s.tick || s.tick<current.tick)
        return reject("order id="+std::to_string(s.id)+" current="+std::to_string(current.id)+" tick="+std::to_string(s.tick)+" current_tick="+std::to_string(current.tick));
    if (!validWorld(s.world)) return reject("invalid world entities="+std::to_string(s.world.size()));
    for (auto& e:s.world)
    {
        const auto who=[&](UActor* a){return " entity="+std::to_string(e.ref.id)+" kind="+std::to_string(int(e.kind))+(a?" class="+a->Class->Name.ToString()+" name="+a->Name.ToString():std::string());};
        auto it=actors.find(e.ref.id);
        if (it==actors.end() || it->second.first!=e.ref) return reject(std::string(it==actors.end()?"unbound":"stale generation")+who(nullptr));
        auto* actor=it->second.second;
        if (actor->bDeleteMe()) return reject("deleted"+who(actor));
        if (!actor->NetworkReplica) return reject("not replica"+who(actor));
        if (kind(actor)!=e.kind) return reject("kind mismatch local="+std::to_string(int(kind(actor)))+who(actor));
        if (!RunePhysics::ValidateReplica(actor,e.physical)) return reject("physics"+who(actor));
        if (!validAnimation(actor,e.bodyAnimation)) return reject("body animation"+who(actor));
        if (!validAnimation(object(actor,"AnimProxy"),e.torsoAnimation)) return reject("torso animation"+who(actor));
        if(e.attachmentJoint!=255)
        {
            auto parent=actors.find(e.owner.id);
            auto* model=parent==actors.end()?nullptr:UObject::TryCast<USkelModel>(GetRuneDisplayMesh(parent->second.second));
            if(!model || e.attachmentJoint>=model->Joints.size())
                return reject("attachment joint="+std::to_string(e.attachmentJoint)+" owner="+std::to_string(e.owner.id)+(model?" joints="+std::to_string(model->Joints.size()):" no model")+who(actor));
        }
    }
    // All potentially failing validation precedes writes. Assignment below invokes
    // neither VM events nor SetLocation/SetOwner collision/script callbacks.
    previous=current; current=s;
    for (auto& [id,item]:actors) { (void)id; item.second->bHidden()=true; item.second->Owner()=nullptr; item.second->ChildActors.clear(); }
    for (auto& e:s.world)
    {
        auto* a=actors.at(e.ref.id).second; a->bHidden()=bool(e.flags&32);
        a->Owner()=e.owner.id?actors.at(e.owner.id).second:nullptr;
        if(a->Owner())a->Owner()->ChildActors.push_back(a);
        RuneCorpse::Presentation(a,e.kind==EntityKind::Corpse && (e.flags&4));
        a->bCarriedItem()=e.attachmentJoint!=255;
        a->SetObject("AttachParent",e.attachmentJoint!=255?a->Owner():nullptr);
        a->SetByte("AttachParentJoint",e.attachmentJoint!=255?e.attachmentJoint:0);
        int health=e.health;
        auto overlay=healthOverlays.find(e.ref.id);
        if(overlay!=healthOverlays.end())
        {
            if(overlay->second.ref==e.ref && overlay->second.life==e.life && s.tick<overlay->second.tick)
                health=overlay->second.health;
            else healthOverlays.erase(overlay);
        }
        if(auto* pawn=UObject::TryCast<UPawn>(a)){pawn->Health()=health;pawn->Spider.active=(e.combat.flags&16)!=0;RuneIK::SetGrounded(a,(e.flags&2)!=0);}
        else if(a->HasProperty("Health"))a->SetInt("Health",health);
        if(e.kind==EntityKind::Weapon)a->SetBool("bPoweredUp",(e.flags&128)!=0);
        if(a->HasProperty("Weapon"))a->SetObject("Weapon",e.weapon.id?actors.at(e.weapon.id).second:nullptr);
        if(a->HasProperty("Shield"))a->SetObject("Shield",e.shield.id?actors.at(e.shield.id).second:nullptr);
        if(auto* p=UObject::TryCast<UPawn>(a)) {
            const auto& c=e.combat;
            const auto* prior=findEntity(previous.world,e.ref);
            double lastWholeBody=prior && prior->life==e.life && prior->combat.offhand==c.offhand?p->Dual.lastWholeBody:-1;
            p->Dual={};p->Dual.lastWholeBody=lastWholeBody;p->Dual.offhand=c.offhand.id?UObject::TryCast<UWeapon>(actors.at(c.offhand.id).second):nullptr;
            RuneFinisher::PresentHands(p,c.flags>>6,c.impaleAim.has_value());
            if(c.impaleAim){
                auto& q=*c.impaleAim;
                p->finisherAimWorld=p->Location()+vec3(float(q[0]),float(q[1]),float(q[2]))*.125f;
                p->finisherAimTime=p->finisherPoseStart;
            }
            p->Dual.followAvailable=(c.flags&32)!=0;p->Dual.left=(c.flags&1)!=0;p->Dual.guard=(c.flags&2)!=0;p->Dual.guardBlend=float(c.guard)/255;
            p->Bloodlust={};p->Bloodlust.active=(c.flags&4)!=0;p->Bloodlust.rushing=(c.flags&8)!=0;
            p->Bloodlust.phase=c.phase;p->Bloodlust.age=float(c.age)/1000;p->Bloodlust.cooldown=float(c.cooldown)/1000;
            p->Slam={};if(c.phase>=6){p->Slam.phase=c.phase-5;p->Slam.age=float(c.age)/1000;p->Bloodlust.phase=0;p->Bloodlust.age=0;}
            p->Bloodlust.rage=c.meter;p->Bloodlust.ready=c.ready;
            if(p->IsA("RunePlayer")) {
                p->SetInt("Strength",int(std::lround(float(c.meter)*p->GetInt("MaxStrength")/100)));
                p->SetInt("RunePower",c.runePower);p->SetInt("MaxPower",c.runeCapacity);
                p->SetBool("bBloodLust",(c.flags&4)!=0);
            }
        }
        setAnimation(a,e.bodyAnimation,e.animationPhase,e.animationRate,bool(e.flags&8));
        setAnimation(object(a,"AnimProxy"),e.torsoAnimation,e.torsoPhase,e.torsoRate,bool(e.flags&16));
    }
    // Rebuild presentation-only inventory from authenticated ownership. This
    // keeps HUD queries and stowed equipment aligned without pickup callbacks.
    for(auto& [id,item]:actors) {
        auto* a=item.second;
        if(a->HasProperty("Inventory"))a->SetObject("Inventory",nullptr);
        if(a->HasProperty("StowSpot"))std::fill_n(static_cast<UObject**>(a->GetProperty("StowSpot")),3,nullptr);
    }
    for(const auto& e:s.world) {
        auto* item=actors.at(e.ref.id).second;auto* owner=item->Owner();
        if(!item->IsA("Inventory") || !owner || !owner->IsA("Pawn"))continue;
        item->SetObject("Inventory",owner->GetUObject("Inventory"));owner->SetObject("Inventory",item);
        if(item->IsA("Weapon") && owner->HasProperty("StowSpot") && e.attachmentJoint!=255 &&
           item!=owner->GetUObject("Weapon") && item!=UObject::Cast<UPawn>(owner)->Dual.offhand && item->GetByte("MeleeType")<3)
            static_cast<UObject**>(owner->GetProperty("StowSpot"))[item->GetByte("MeleeType")]=item;
    }
    for(auto it=healthOverlays.begin();it!=healthOverlays.end();)
    {
        if(!findEntity(s.world,it->second.ref))it=healthOverlays.erase(it);
        else ++it;
    }
    // A new snapshot starts a presentation interval. Showing its endpoint here
    // would snap forward, then the next render frame would jump back to the
    // previous snapshot before blending forward again.
    present(previous.id?0:1); return true;
}
RuneNetworkReplica::CombatApply RuneNetworkReplica::applyCombat(const CombatOutcome& c)
{
    if(c.id<=lastCombatId)return CombatApply::Superseded; // Already consumed; never run damage twice.
    if(c.id!=lastCombatId+1 || !current.id)return CombatApply::Rejected;
    Bytes validation;
    if(!encodeCombatOutcome(1,c,validation))return CombatApply::Rejected;
    auto classify=[&](EntityRef ref,uint32_t life)->int
    {
        auto* e=findEntity(current.world,ref);
        if(!e)return current.tick>=c.tick?1:-1;
        if(e->ref.generation>ref.generation ||
            (e->ref==ref && e->life>life))return 1; // Later spawn/life supersedes this hit.
        if(e->ref.generation<ref.generation || e->life<life)return -1;
        return e->ref==ref && e->kind==EntityKind::Pawn ? 0:2;
    };
    int source=classify(c.attacker,c.attackerLife);
    int target=c.target.id?classify(c.target,c.targetLife):0;
    if(source==2 || target==2)return CombatApply::Rejected;
    if(source<0 || target<0)return CombatApply::Deferred;
    auto presentBash=[&]() {
        if(!source && !target && c.target.id && c.contact==CombatContact::Hit && c.targetHealth>0 && (current.tick<=c.tick || current.tick-c.tick<=15)) {
            auto v=actors.find(c.target.id),a=actors.find(c.attacker.id);
            if(v!=actors.end() && v->second.first==c.target && a!=actors.end() && a->second.first==c.attacker) {
                auto* pawn=UObject::TryCast<UPawn>(v->second.second);
                auto direction=v->second.second->Location()-a->second.second->Location();
                if(length(direction)>1)direction=normalize(direction)*400.f;
                RunePhysics::React(pawn,v->second.second->Location()+vec3(0,0,20),direction);
            }
        }

        if(source || target || !c.target.id || c.move!=CombatMove::Bash || c.contact!=CombatContact::Hit || c.targetHealth<=0)return;
        // Late reliable outcomes must not replay an ancient reaction or affect a new life.
        if(current.tick>c.tick && current.tick-c.tick>15)return;
        auto it=actors.find(c.target.id);
        if(it!=actors.end() && it->second.first==c.target)
            RuneBash::PresentImpact(UObject::TryCast<UPawn>(it->second.second),c.staggered);
    };
    if(source>0 || target>0 || current.tick>=c.tick)
    { presentBash(); lastCombatId=c.id; return CombatApply::Superseded; }
    auto sourceActor=actors.find(c.attacker.id);
    auto targetActor=c.target.id?actors.find(c.target.id):actors.end();
    if(sourceActor==actors.end() || sourceActor->second.first!=c.attacker ||
       (c.target.id && (targetActor==actors.end() || targetActor->second.first!=c.target)))
        return CombatApply::Deferred;
    if(c.target.id)
    {
        auto* pawn=UObject::TryCast<UPawn>(targetActor->second.second);
        if(!pawn || pawn->bDeleteMe() || !pawn->NetworkReplica)return CombatApply::Rejected;
        pawn->Health()=c.targetHealth; // Data write only; no JointDamaged or physics callback.
        healthOverlays[c.target.id]={c.target,c.targetLife,c.tick,c.targetHealth};
    }
    presentBash();lastCombatId=c.id; return CombatApply::Applied;
}
bool RuneNetworkReplica::queueCombat(const CombatOutcome& c,uint64_t localNow)
{
    if(c.id<=lastCombatId)return true;
    if(pendingOutcomes.size()>=64 || lastCombatId==UINT64_MAX)return false;
    uint64_t expected=pendingOutcomes.empty()?lastCombatId+1:pendingOutcomes.back().outcome.id+1;
    if(c.id!=expected)return false;
    pendingOutcomes.push_back({c,localNow});return true;
}
RuneNetworkReplica::CombatQueue RuneNetworkReplica::flushCombat(uint64_t localNow)
{
    while(!pendingOutcomes.empty())
    {
        auto& item=pendingOutcomes.front();
        if(localNow<item.since)return CombatQueue::Fault;
        auto state=applyCombat(item.outcome);
        if(state==CombatApply::Deferred)
        {
            auto wait=localNow-item.since;
            if(wait>=SnapshotTransferTimeout*2)return CombatQueue::Fault;
            return wait>=SnapshotTransferTimeout?CombatQueue::Resync:CombatQueue::Waiting;
        }
        if(state==CombatApply::Rejected)return CombatQueue::Fault;
        resolvedOutcomes.push_back(item.outcome);pendingOutcomes.pop_front();
    }
    return CombatQueue::Ready;
}
std::vector<CombatOutcome> RuneNetworkReplica::drainResolvedCombat()
{
    auto out=std::move(resolvedOutcomes);resolvedOutcomes.clear();return out;
}
void RuneNetworkReplica::clearAnticipation()
{
    if(auto* p=actor(anticipation.ref)) {
        p->RuneCombatPreviewGuard=-1;
        if(auto* proxy=object(p,"AnimProxy")) {
            proxy->RuneCombatPreviewWeight=0;proxy->RuneCombatPreviewModel=nullptr;
            proxy->RuneCombatPreviewPose.clear();
        }
    }
    anticipation={};
}
bool RuneNetworkReplica::anticipate(EntityRef ref,uint64_t id,ActionKind kind,const Input& input,uint64_t now)
{
    if(kind!=ActionKind::Attack && kind!=ActionKind::RaiseShield && kind!=ActionKind::LowerShield) {
        clearAnticipation();return false;
    }
    const auto* e=findEntity(current.world,ref);
    auto* p=UObject::TryCast<UPawn>(actor(ref));auto* proxy=object(p,"AnimProxy");
    auto* model=proxy?UObject::TryCast<USkelModel>(GetRuneDisplayMesh(proxy)):nullptr;
    if(!id || !e || !p || !p->NetworkReplica || !proxy || !model || e->health<=0 ||
       input.life!=e->life || e->physical.kind || e->combat.phase || RuneCorpse::Busy(p) || (e->flags&32))return false;
    auto* weapon=object(p,"Weapon");
    auto authored=[&](const char* field){return weapon && weapon->HasProperty(field)?weapon->GetName(field):NameString("None");};
    // Never invent a combo or interrupt an authoritative attack/recovery pose.
    bool attacking=false;
    for(auto field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackStandA","A_AttackStandB",
        "A_AttackBackupA","A_AttackBackupB","A_AttackStrafeRight","A_AttackStrafeLeft","A_JumpAttack",
        "A_AttackAReturn","A_AttackBReturn","A_AttackCReturn","A_AttackDReturn","A_AttackStandAReturn","A_AttackStandBReturn",
        "A_AttackBackupAReturn","A_AttackBackupBReturn"})
        if(!authored(field).IsNone() && authored(field)==proxy->AnimSequence())attacking=true;
    bool locomotion=(e->flags&16)!=0 || proxy->AnimSequence()=="neutral_idle";
    for(auto field:{"A_Idle","A_Forward","A_Backward","A_StrafeRight","A_StrafeLeft","A_Forward45Right","A_Forward45Left","A_Backward45Right","A_Backward45Left","A_TurnRight","A_TurnLeft"})
        if(!authored(field).IsNone() && authored(field)==proxy->AnimSequence())locomotion=true;
    if(kind!=ActionKind::LowerShield && (attacking || !locomotion))return false;
    if(kind==ActionKind::Attack && (anticipation.action || !weapon || (e->flags&4) || (e->combat.flags&2)))return false;
    if(kind!=ActionKind::Attack && !e->shield.id && !e->combat.offhand.id)return false;
    if(kind==ActionKind::LowerShield && !(e->flags&4) && !(e->combat.flags&2) &&
       !(anticipation.action && anticipation.kind==ActionKind::RaiseShield))return false;
    NameString sequence("None");
    if(kind==ActionKind::Attack) {
        double speed=std::hypot(double(p->Velocity().x),double(p->Velocity().y));
        double magnitude=std::hypot(double(input.forward),double(input.strafe));
        double forward=magnitude?input.forward/magnitude:0;
        const char* field=(input.held&4)?(input.strafe<0?"A_AttackStrafeLeft":"A_AttackStrafeRight"):
            speed*speed<1000?"A_AttackStandA":forward>.9?"A_AttackA":forward<-.9?"A_AttackBackupA":
            input.strafe<0?"A_AttackStrafeLeft":"A_AttackStrafeRight";
        sequence=authored(field);
    } else if(!e->combat.offhand.id) {
        sequence=kind==ActionKind::LowerShield?(weapon?authored("A_Idle"):NameString("neutral_idle")):weapon?authored("A_Defend"):NameString("h3_defendTO");
    }
    uint16_t index=0;
    for(size_t i=0;i<model->Sequences.size();++i)if(model->Sequences[i].Info.Name==sequence){index=uint16_t(i+1);break;}
    if(!index && !e->combat.offhand.id)return false;
    if(kind==ActionKind::Attack && !index)return false;
    auto source=model->SampleAnimation(proxy);
    if(proxy->RuneCombatPreviewModel==model && proxy->RuneCombatPreviewPose.size()==source.size())
        for(size_t j=0;j<source.size();++j)source[j]=BlendRunePose(source[j],proxy->RuneCombatPreviewPose[j],proxy->RuneCombatPreviewWeight);
    float guard=p->RuneCombatPreviewGuard>=0?p->RuneCombatPreviewGuard:p->Dual.guardBlend;
    clearAnticipation();
    anticipation.ref=ref;anticipation.weapon=e->weapon;anticipation.shield=e->shield;anticipation.offhand=e->combat.offhand;
    anticipation.life=e->life;anticipation.health=e->health;anticipation.action=id;anticipation.started=now;anticipation.snapshot=current.id;
    anticipation.model=model;anticipation.kind=kind;anticipation.sequence=index;anticipation.tween=kind==ActionKind::Attack?.1f:kind==ActionKind::RaiseShield?.01f:.15f;
    anticipation.rate=kind==ActionKind::RaiseShield?1.5f:1.f;anticipation.guardStart=guard;
    anticipation.startPose.assign(source.begin(),source.end());
    presentAnticipation(now);return true;
}
void RuneNetworkReplica::resolveAnticipation(const ActionResult& result,uint64_t now)
{
    if(result.id==anticipation.action && result.status!=Admission::Admitted && result.status!=Admission::Started && !anticipation.settle)
        anticipation.settle=now;
}
void RuneNetworkReplica::presentAnticipation(uint64_t now)
{
    auto& a=anticipation;if(!a.action)return;
    const auto* e=findEntity(current.world,a.ref);auto* p=UObject::TryCast<UPawn>(actor(a.ref));
    auto* proxy=object(p,"AnimProxy");auto* model=proxy?UObject::TryCast<USkelModel>(GetRuneDisplayMesh(proxy)):nullptr;
    if(!e || !p || !proxy || !model || model!=a.model || now<a.started || e->life!=a.life || e->health<=0 || e->health<a.health || p->Health()<=0 || p->Health()<a.health ||
        e->weapon!=a.weapon || e->shield!=a.shield || e->combat.offhand!=a.offhand || e->physical.kind || e->combat.phase || RuneCorpse::Busy(p) || (e->flags&32)) {
        clearAnticipation();return;
    }
    bool confirmed=false;
    if(current.id>a.snapshot) {
        if(a.kind==ActionKind::Attack) {
            confirmed=a.sequence && e->torsoAnimation==a.sequence;
            // A direction change can make the host choose another valid swing.
            // Hand that choice back promptly instead of holding the guessed pose.
            if(auto* weapon=object(p,"Weapon"))for(auto field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackStandA","A_AttackStandB","A_AttackBackupA","A_AttackBackupB","A_AttackStrafeRight","A_AttackStrafeLeft","A_JumpAttack"})
                if(!weapon->GetName(field).IsNone() && weapon->GetName(field)==proxy->AnimSequence())confirmed=true;
        }
        else {bool raised=(e->flags&4) || (e->combat.flags&2);confirmed=raised==(a.kind==ActionKind::RaiseShield);}
    }
    if(!a.settle && (confirmed || now-a.started>=350))a.settle=now;
    float weight=a.settle?std::clamp(1.f-float(now-a.settle)/80.f,0.f,1.f):1.f;
    if(weight<=0){clearAnticipation();return;}
    float seconds=float(now-a.started)/1000.f;
    if(a.offhand.id && a.kind!=ActionKind::Attack) {
        float target=a.kind==ActionKind::RaiseShield?1.f:0.f;
        float predicted=a.guardStart+(target-a.guardStart)*std::clamp(seconds/.12f,0.f,1.f);
        p->RuneCombatPreviewGuard=p->Dual.guardBlend+(predicted-p->Dual.guardBlend)*weight;
    } else if(a.sequence && a.sequence<=model->Sequences.size()) {
        const auto& info=model->Sequences[a.sequence-1].Info;
        // Predict only the authored attack tween, never a speculative strike.
        float phase=a.kind==ActionKind::Attack?0.f:std::max(0.f,seconds-a.tween)*a.rate*info.Rate/std::max(1,info.NumFrames);
        auto pose=model->SampleFrame(a.sequence-1,phase,false);
        if(pose.size()!=a.startPose.size()){clearAnticipation();return;}
        float mix=std::clamp(seconds/a.tween,0.f,1.f);
        for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(a.startPose[j],pose[j],mix);
        proxy->RuneCombatPreviewModel=model;proxy->RuneCombatPreviewPose=std::move(pose);proxy->RuneCombatPreviewWeight=weight;
    }
}

void RuneNetworkReplica::present(double alpha,double animationSeconds)
{presentFrames(alpha,nullptr,nullptr,1,{},animationSeconds);}
void RuneNetworkReplica::presentAdaptive(const Snapshot& before,const Snapshot& after,double alpha,EntityRef owned,double localAlpha,double animationSeconds)
{presentFrames(localAlpha,&before,&after,alpha,owned,animationSeconds);}
void RuneNetworkReplica::presentFrames(double alpha,const Snapshot* older,const Snapshot* newer,double remoteAlpha,EntityRef owned,double animationSeconds)
{
    auto nearby=[](const Entity& x,const Entity& y){double d=0;for(int i=0;i<3;++i){auto v=double(x.position[i])-y.position[i];d+=v*v;}return d<=2048.*2048;};
    for(const auto& e:current.world)
    {
        const Entity* legacyBefore=findEntity(previous.world,e.ref);
        if(legacyBefore && legacyBefore->ref==e.ref && !nearby(*legacyBefore,e)){adaptiveLives.erase(e.ref.id);animationPlayback.erase(e.ref.id);}
        if(!legacyBefore || legacyBefore->ref!=e.ref || !nearby(*legacyBefore,e))legacyBefore=&e;
        // Centred carried/flight coordinates and the authored ground-pose
        // origin are different bases. Never interpolate between those bases.
        if(e.kind==EntityKind::Corpse && ((legacyBefore->flags^e.flags)&4))legacyBefore=&e;
        auto binding=actors.find(e.ref.id);
        if(binding==actors.end() || binding->second.first!=e.ref)continue; // Despawn may lead its next snapshot.
        auto* a=binding->second.second;
        const Entity* before=legacyBefore;const Entity* after=&e;double mix=alpha;
        // Local prediction and collidable movers keep their own current timeline.
        // Never resurrect a retired life, old equipment ownership or a teleport.
        if(older && newer && e.ref!=owned && e.kind==EntityKind::Pawn && a->GetInt("Health")>0) {
            auto* left=findEntity(older->world,e.ref);auto* right=findEntity(newer->world,e.ref);
            if(left && right && left->ref==e.ref && right->ref==e.ref && left->life==e.life && right->life==e.life &&
                left->owner==e.owner && right->owner==e.owner && right->attachmentJoint==e.attachmentJoint &&
                (right->flags&33)==(e.flags&33) && nearby(*right,e)) {
                auto& state=adaptiveLives[e.ref.id];if(state.first!=e.life)state={e.life,false};
                // A newly spawned life starts on the current timeline. Adopt
                // buffering while stationary so it cannot rewind its first run.
                double separation=0;for(int i=0;i<3;++i)separation+=std::abs(double(left->position[i])-e.position[i]);
                if(separation<=8)state.second=true;
                bool moverSupport=false;
                if(state.second) {
                    vec3 hit,normal,center(float(e.position[0])/8,float(e.position[1])/8,float(e.position[2])/8);
                    auto* support=UObject::TryCast<UActor>(a->Trace(hit,normal,center-vec3(0,0,a->CollisionHeight()+96),center,true,vec3(0)));
                    moverSupport=GetMovingBrush(support)!=nullptr;
                }
                if(moverSupport)state.second=false;
                if(state.second && !moverSupport){before=nearby(*left,*right)?left:right;after=right;mix=remoteAlpha;}
            }
        }
        auto pose=interpolatePose(*before,*after,mix);
        const auto animation=interpolatePose(*legacyBefore,e,alpha);
        // Buffer root motion only. Keep the existing fractional skeletal
        // interpolation and prompt attack/block feedback on the latest worlds.
        pose.bodyPhase=animation.bodyPhase;pose.bodyRate=animation.bodyRate;
        pose.torsoPhase=animation.torsoPhase;pose.torsoRate=animation.torsoRate;
        vec3 location(float(pose.position[0]),float(pose.position[1]),float(pose.position[2]));
        Rotator rotation(int(pose.angles[0]),int(pose.angles[1]),int(pose.angles[2]));
        bool moved=location!=a->Location() || rotation.Pitch!=a->Rotation().Pitch ||
            rotation.Yaw!=a->Rotation().Yaw || rotation.Roll!=a->Rotation().Roll;
        bool rehash=e.kind==EntityKind::Mover && a->XLevel() && moved;
        if(rehash)a->XLevel()->Collision.RemoveFromCollision(a);
        a->Location()=location;a->Rotation()=rotation;
        RunePhysics::Present(a,legacyBefore->physical,e.physical,float(alpha));
        for(int axis=0;axis<3;++axis)a->Velocity()[axis]=float(pose.velocity[axis]);
        if(rehash)a->XLevel()->Collision.AddToCollision(a);
        // Assign regions without zone-change scripts or local swimming physics.
        a->Region()=a->FindRegion();
        if(auto* pawn=UObject::TryCast<UPawn>(a))
        {pawn->FootRegion()=a->FindRegion(vec3(0,0,-a->CollisionHeight()));
            pawn->HeadRegion()=a->FindRegion(vec3(0,0,pawn->EyeHeight()));}
        auto transitionPhase=[&](UActor* actor,uint16_t sequence,double phase,bool changed,double amount)
        {
            if(!changed || phase>=0 || !actor)return phase;
            auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
            if(!model || !sequence || sequence>model->Sequences.size())return phase;
            double mix=std::clamp(std::isfinite(amount)?amount:1.0,0.0,1.0);
            double start=-1.0/std::max(1,model->Sequences[sequence-1].Info.NumFrames);
            return start*(1-mix)+phase*mix;
        };
        if(auto* p=UObject::TryCast<UPawn>(a)) {
            float blend=float(std::clamp(std::isfinite(alpha)?alpha:1.0,0.0,1.0));
            if(legacyBefore->life==e.life && legacyBefore->combat.offhand==e.combat.offhand)
                p->Dual.guardBlend=(legacyBefore->combat.guard+(e.combat.guard-legacyBefore->combat.guard)*blend)/255.f;
            if(legacyBefore->life==e.life && legacyBefore->combat.phase==e.combat.phase && legacyBefore->combat.age<=e.combat.age) {
                float age=(legacyBefore->combat.age+(e.combat.age-legacyBefore->combat.age)*blend)/1000.f;
                if(e.combat.phase>=6)p->Slam.age=age;else p->Bloodlust.age=age;
            }
        }
        const bool playback=std::isfinite(animationSeconds) && animationSeconds>=0 &&
            e.kind==EntityKind::Pawn && e.health>0 && a->GetInt("Health")>0 && !e.physical.kind && !e.combat.phase;
        // install() also prepares legacy root placement before the render call.
        // That intermediate pass must not discard the persistent playback clock.
        if(e.kind!=EntityKind::Pawn || e.health<=0 || e.physical.kind || e.combat.phase)animationPlayback.erase(e.ref.id);
        auto animationFrame=[&](UActor* actor,uint16_t sequence,int16_t phase,int16_t rate,bool loop,double fallback,bool torso) {
            if(!playback || !actor || !sequence)return fallback;
            auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
            if(!model || sequence>model->Sequences.size())return fallback;
            auto& channels=animationPlayback[e.ref.id];
            auto& clock=torso?channels.torso:channels.body;
            RuneAnimationPlayback::Sample sample;
            sample.snapshot=current.id;sample.tick=current.tick;sample.life=e.life;sample.sequence=sequence;
            sample.model=reinterpret_cast<uintptr_t>(model);sample.phase=double(phase)/32767;
            sample.rate=double(rate)/256;sample.end=1.-1./std::max(1,model->Sequences[sequence-1].Info.NumFrames);
            sample.looping=loop;
            return clock.present(sample,animationSeconds,fallback);
        };
        if(e.bodyAnimation)
        {
            double frame=transitionPhase(a,e.bodyAnimation,pose.bodyPhase,legacyBefore!=&e &&
                legacyBefore->life==e.life && legacyBefore->bodyAnimation!=e.bodyAnimation,alpha);
            a->AnimFrame()=float(animationFrame(a,e.bodyAnimation,e.animationPhase,e.animationRate,bool(e.flags&8),frame,false));a->AnimRate()=float(pose.bodyRate);
        }
        if(e.torsoAnimation)if(auto* proxy=object(a,"AnimProxy"))
        {
            double frame=transitionPhase(proxy,e.torsoAnimation,pose.torsoPhase,legacyBefore!=&e &&
                legacyBefore->life==e.life && legacyBefore->torsoAnimation!=e.torsoAnimation,alpha);
            proxy->AnimFrame()=float(animationFrame(proxy,e.torsoAnimation,e.torsoPhase,e.torsoRate,bool(e.flags&16),frame,true));proxy->AnimRate()=float(pose.torsoRate);
        }
        if(auto* p=UObject::TryCast<UPawn>(a))RuneDualWield::UpdatePoseState(p);
    }
}

namespace {
CollisionHit PredictionSweep(Engine& game,UPlayerPawn* player,vec3 position,vec3 delta,bool resolveTangency=false)
{
    CollisionHit result;float distance=length(delta);if(distance<.00001f)return result;
    for(const auto& hit:game.Level->Collision.Trace(position,position+delta,
        player->CollisionHeight(),player->CollisionRadius(),true,true,false))
    {
        if(hit.Actor==player || !hit.Actor || hit.Actor->bDeleteMe())continue;
        if(hit.Actor==game.LevelInfo || (hit.Actor->bBlockPlayers() && !hit.Actor->IsBasedOn(player)))
        {
            // Quantized/replayed positions can rest on the trace's contact
            // skin. Re-sweep a quarter unit outside a flat wall when moving
            // parallel to it; do not simply discard the hit, since another
            // wall may still block the requested path. Prediction itself uses
            // the exact original sweep; this applies only to presentation.
            if(resolveTangency && hit.Fraction<.0001f && !hit.Actor->IsA("Pawn") &&
               std::abs(hit.Normal.z)<.2f && length(hit.Normal)>.5f &&
               std::abs(delta.z)<.001f && std::abs(dot(delta,hit.Normal))<.001f) {
                auto biased=position+hit.Normal*.25f;
                auto clear=player->CheckLocation(biased,std::max(.1f,player->CollisionRadius()-.25f),std::max(.1f,player->CollisionHeight()-.25f),true);
                if(clear.first && length(clear.second-biased)<.001f)
                    return PredictionSweep(game,player,biased,delta,false);
            }
            result=hit;break;
        }
    }
    // Replica pawns deliberately are not in the gameplay collision hash.
    // Query their cylinders without inserting them or dispatching Touch events.
    for(auto* actor:game.Level->Actors) {
        auto* other=UObject::TryCast<UPawn>(actor);
        if(!other || other==player || !other->NetworkReplica || other->bDeleteMe() || other->bHidden() || other->Health()<=0)continue;
        double t=TraceTester::CylinderCylinderTrace(dvec3(position),dvec3(delta/distance),0,distance,
            dvec3(other->Location()),other->CollisionHeight(),other->CollisionRadius(),player->CollisionHeight(),player->CollisionRadius());
        if(t>=0 && t<distance*result.Fraction) {
            result.Fraction=float(t/distance);result.Actor=other;
            auto normal=position+delta*result.Fraction-other->Location();normal.z=0;
            result.Normal=length(normal)>.001f?normalize(normal):-(delta/distance);
        }
    }
    return result;
}
}
bool StepRunePredictedMovement(Engine& game,UPlayerPawn* player,MovementPose& pose,
    const Input& input,const MovementAck& ack,float dt)
{
    if(!player || !player->NetworkReplica || !ack.mode || ack.mode>MovementFalling ||
        pose.mode==3 || (input.held&4) || dt<=0 || dt>.02f)return false;
    vec3 position(pose.position[0],pose.position[1],pose.position[2]);
    vec3 velocity(pose.velocity[0],pose.velocity[1],0);
    auto original=position;
    float yaw=float(input.yaw)*(6.28318530718f/65536);
    float forward=float(input.forward),strafe=float(input.strafe);
    float size=std::hypot(forward,strafe);if(size>1000){forward*=1000/size;strafe*=1000/size;}
    // Matches the native input scale and PlayerWalking's authored 0.4 factor.
    vec3 acceleration((forward*std::cos(yaw)-strafe*std::sin(yaw))*2.4f,
        (forward*std::sin(yaw)+strafe*std::cos(yaw))*2.4f,0);
    if(ack.mode==MovementFalling) {
        velocity.z=pose.velocity[2];
        float accel=length(acceleration);if(accel>ack.acceleration)acceleration*=float(ack.acceleration)/accel;
        // Match TickFalling's half-step equation, including its total-speed cap.
        auto newVelocity=velocity+(acceleration*1.5f+vec3(0,0,-2.f*ack.friction))*(.5f*dt);
        float speed2=dot(velocity.xy(),velocity.xy()),maximum=float(ack.speed)/8;
        if(speed2>=maximum*maximum && dot(newVelocity.xy(),newVelocity.xy())>speed2) {
            auto xy=normalize(newVelocity.xy())*std::sqrt(speed2);newVelocity.x=xy.x;newVelocity.y=xy.y;
        }
        float terminal=float(ack.step)/8;if(length(newVelocity)>terminal)newVelocity=normalize(newVelocity)*terminal;
        auto delta=newVelocity*dt;auto hit=PredictionSweep(game,player,position,delta);
        // Dynamic contacts, water and grabs must be resolved by the authority.
        if(hit.Fraction<1 && hit.Actor!=game.LevelInfo && (!hit.Actor || !hit.Actor->bStatic()))return false;
        position+=delta*hit.Fraction;uint8_t mode=MovementFalling;
        if(hit.Fraction<1) {
            if(hit.Normal.z>=.7071f){newVelocity.z=0;mode=3;}
            else {
                auto slide=(delta-hit.Normal*dot(delta,hit.Normal))*(1-hit.Fraction);
                if(dot(delta,slide)>=0) {
                    auto contact=PredictionSweep(game,player,position,slide);
                    if(contact.Fraction<1 && contact.Actor!=game.LevelInfo && (!contact.Actor || !contact.Actor->bStatic()))return false;
                    position+=slide*contact.Fraction;
                    if(contact.Fraction<1 && contact.Normal.z>=.7071f)mode=3;
                }
                newVelocity=(position-original)/dt;if(mode==3)newVelocity.z=0;
            }
        }
        auto region=game.Level->Model->FindRegion(position,game.LevelInfo);
        auto foot=game.Level->Model->FindRegion(position-vec3(0,0,player->CollisionHeight()),game.LevelInfo);
        if(!region.Zone || !foot.Zone || region.Zone->bWaterZone() || foot.Zone->bWaterZone() ||
            length(region.Zone->ZoneVelocity())>.001f || std::abs(region.Zone->ZoneGravity().x)>.001f || std::abs(region.Zone->ZoneGravity().y)>.001f ||
            std::abs(region.Zone->ZoneGravity().z+ack.friction)>.51f)return false;
        for(int axis=0;axis<3;++axis){pose.position[axis]=position[axis];pose.velocity[axis]=newVelocity[axis];}
        pose.mode=mode;return true;
    }
    ApplyMovementVelocity(velocity,acceleration,dt,ack.acceleration,float(ack.friction)/256,float(ack.speed)/8);
    auto move=[&](vec3 delta){auto hit=PredictionSweep(game,player,position,delta);position+=delta*hit.Fraction;return hit;};
    float step=float(ack.step)/8;
    auto ground=[&](float height){auto h=PredictionSweep(game,player,position,vec3(0,0,-height));
        if(h.Fraction>=1 || h.Normal.z<.7071f)return false;
        position.z-=height*h.Fraction;return true;};
    float remaining=dt;
    if(length(velocity)>0)for(int iteration=0;iteration<5 && remaining>0;++iteration) {
        auto hit=move(velocity*remaining);remaining*=1-hit.Fraction;
        vec3 delta=velocity*remaining;
        if(!ground(step*1.3f))return false; // Ledges/landing remain authoritative.
        if(hit.Fraction<1) {
            move(vec3(0,0,step));hit=move(delta);remaining*=1-hit.Fraction;
            if(!ground(step*2.6f))return false;
            move(vec3(0,0,-step));
        }
        if(hit.Fraction<1 && hit.Actor==game.LevelInfo && std::abs(hit.Normal.z)<.2f) {
            auto slide=(delta-hit.Normal*dot(delta,hit.Normal))*(1-hit.Fraction);
            if(dot(delta,slide)>=0){hit=move(slide);remaining*=1-hit.Fraction;}else remaining=0;
        }
        if(!ground(step*1.3f))return false;
    } else if(!ground(step*1.3f))return false;
    velocity=(position-original)/dt;velocity.z=0;
    for(int axis=0;axis<3;++axis){pose.position[axis]=position[axis];pose.velocity[axis]=velocity[axis];}
    pose.mode=MovementWalking;
    return true;
}
namespace {
// Network pawns receive animation phases directly and never run attack notifies.
// Read the authored swipe interval without dispatching combat scripts on a replica.
bool swipeWindow(UActor* actor,uint16_t sequence,UWeapon* weapon)
{
    if(!actor || !sequence || !weapon || actor->bAnimLoop() || actor->AnimFrame()<0)return false;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
    if(!model || sequence>model->Sequences.size())return false;
    const auto& info=model->Sequences[sequence-1].Info;
    if(info.Name!=actor->AnimSequence())return false;
    // Original RunePlayerProxy also starts the swipe on entry to Attacking.
    // Older hosts send the equipped weapon and animation but no blade-state bit.
    static const NameString attacks[]={"A_AttackA","A_AttackB","A_AttackC","A_AttackD",
        "A_AttackStandA","A_AttackStandB","A_AttackBackupA","A_AttackBackupB",
        "A_AttackStrafeRight","A_AttackStrafeLeft","A_JumpAttack"};
    bool active=false;
    for(auto& field:attacks)if(weapon->GetName(field)==info.Name){active=true;break;}
    for(const auto& notify:info.Notifys) {
        if(notify.Time>actor->AnimFrame())continue;
        if(notify.Function=="SwipeEffectStart")active=true;
        if(notify.Function=="SwipeEffectEnd")active=false;
    }
    return active;
}
void presentBladeSwipe(UWeapon* weapon,bool active,float dt)
{
    if(!weapon || !weapon->NetworkReplica)return;
    auto* swipe=object(weapon,"Swipe");
    if(!active && (!swipe || !swipe->GetBool("IsLoaded")))return;
    auto* cls=UObject::TryCast<UClass>(weapon->GetUObject(weapon->GetBool("bPoweredUp")?"PoweredUpSwipeClass":"SwipeClass"));
    if(swipe && swipe->Class!=cls){weapon->SetObject("Swipe",nullptr);swipe=nullptr;}
    if(!active && !swipe)return;
    if(active && !swipe) {
        UStruct* base=cls;
        while(base && base->Name!="WeaponSwipe")base=base->BaseStruct;
        if(!base)return;
        swipe=UObject::Cast<UActor>(engine->packages->GetTransientPackage()->NewObject("RemoteSwipe",cls,ObjectFlags::Transient));
        swipe->XLevel()=weapon->XLevel();swipe->Level()=weapon->Level();swipe->Owner()=weapon;
        swipe->SetByte("BaseJointIndex",weapon->GetInt("SweepJoint1"));
        swipe->SetByte("OffsetJointIndex",weapon->GetInt("SweepJoint2"));
        swipe->SetBool("IsLoaded",false);weapon->SetObject("Swipe",swipe);
    }
    const auto elapsed=ExpressionValue::FloatValue(dt);
    // This particle object is drawn with its weapon, outside the gameplay actor list.
    // Reuse its short strip of authored quads and let released strips fade normally.
    if(active) {
        vec3 origin=weapon->Owner()->Location();
        if(!swipe->GetBool("IsLoaded") || swipe->GetFloat("SystemLifeSpan")>=0 || dt>.12f || length(origin-swipe->Location())>96.f)
            CallEvent(swipe,"SystemInit");
        swipe->Location()=origin;
        swipe->SetFloat("SystemLifeSpan",-1.f);
        CallEvent(swipe,"SwipeSystemTick",{elapsed});
    } else swipe->SetFloat("SystemLifeSpan",0.f);
    CallEvent(swipe,"SwipeTick",{elapsed});
    if(!active) {
        RuneParticles::ArrayView particles(swipe);
        for(int i=0;i<std::min(64,int(swipe->GetByte("ParticleCount")));++i)if(particles.Valid(i))return;
        swipe->SetBool("IsLoaded",false);
    }
}
}
void RuneNetworkReplica::tickEffects(float dt) {
    if(!std::isfinite(dt) || dt<=0)return;
    for(const auto& e:current.world) {
        if(e.kind!=EntityKind::Weapon)continue;
        auto* w=UObject::TryCast<UWeapon>(actor(e.ref));
        if(!w)continue;
        auto* pawn=UObject::TryCast<UPawn>(w->Owner());
        if(w->bHidden() || !pawn || pawn->Health()<=0 || pawn->bHidden() ||
           (w!=pawn->Weapon() && w!=pawn->Dual.offhand)) {
            if(auto* swipe=object(w,"Swipe"))swipe->SetBool("IsLoaded",false);
            continue;
        }
        const auto* owner=findEntity(current.world,e.owner);
        bool active=(e.flags&4)!=0;
        if(!active && owner && !owner->combat.phase && !(owner->combat.flags&2) &&
           (pawn->Dual.left ? w==pawn->Dual.offhand : w==pawn->Weapon())) {
            auto* proxy=object(pawn,"AnimProxy");
            active=swipeWindow(proxy,owner->torsoAnimation,pawn->Weapon()) ||
                   (!owner->torsoAnimation && swipeWindow(pawn,owner->bodyAnimation,pawn->Weapon()));
        }
        presentBladeSwipe(w,active,dt);
    }
    for(const auto& [id,item]:actors) {
        auto* a=item.second;if(kind(a)!=EntityKind::Effect || a->bDeleteMe())continue;
        auto& state=effectAges[id];state+=dt;
        if(state>4.f){a->bHidden()=true;continue;}
        if(a->bHidden())continue;
        RuneParticles::Tick(a,std::min(dt,.1f));
        if(a->bHidden())state=5.f; // Keep expired emitters hidden until server despawn.
    }
}
RuneStepProbe ProbeRuneStepAhead(Engine& game,UPlayerPawn* p)
{
    RuneStepProbe out;if(!p || !game.Level)return out;
    const auto at=p->Location();out.x=at.x;out.y=at.y;out.z=at.z;out.limit=p->MaxStepHeight();
    const float yaw=p->Rotation().YawRadians(),reach=std::max(4.f,out.limit);
    const vec3 forward(std::cos(yaw),std::sin(yaw),0);
    // Capsule centre heights on both floors; their difference is the floor step.
    auto here=PredictionSweep(game,p,at,vec3(0,0,-reach*2));
    if(here.Fraction>=1)return out;
    const float floorHere=at.z-reach*2*here.Fraction;
    const vec3 start=vec3(at.x,at.y,floorHere)+forward*(p->CollisionRadius()*2+4)+vec3(0,0,reach*3);
    auto down=PredictionSweep(game,p,start,vec3(0,0,-reach*5));
    if(down.Fraction<=0 || down.Fraction>=1)return out;
    out.ahead=(start.z-reach*5*down.Fraction)-floorHere;out.found=true;return out;
}
void PresentRunePredictedMovement(UPlayerPawn* player,const MovementPose& pose)
{
    if(!player || !player->NetworkReplica)return;
    auto sweep=[&](vec3 at,vec3 delta){return PredictionSweep(*engine,player,at,delta,true);};
    vec3 desired(pose.position[0],pose.position[1],pose.position[2]);
    // Reconciliation smoothing cannot visually carry the player through a wall.
    const auto origin=player->Location();
    auto delta=desired-origin;
    auto hit=sweep(origin,delta);
    auto resolved=origin+delta*hit.Fraction;
    // A straight diagonal sweep hits a stair riser even when prediction has
    // already traversed it with step-up/forward/down sweeps. Follow that safe
    // route for short reconciliations, including a low step whose endpoints
    // share a floor height; never bypass wall collision.
    const float height=std::max(player->MaxStepHeight(),std::abs(delta.z)+1.f);
    if(pose.mode!=MovementFalling && pose.mode!=3 && hit.Fraction<1 && height<=64.f && length(delta)<160.f) {
        auto raised=origin;
        vec3 up(0,0,height);auto ceiling=sweep(raised,up);raised+=up*ceiling.Fraction;
        vec3 across(delta.x,delta.y,0);auto side=sweep(raised,across);raised+=across*side.Fraction;
        vec3 down(0,0,desired.z-raised.z);auto floor=sweep(raised,down);raised+=down*floor.Fraction;
        // A collision may safely leave the capsule on the tread just above
        // the smoothed endpoint. Reject routes blocked horizontally or upward.
        if(ceiling.Fraction==1 && side.Fraction==1 &&
           (floor.Fraction==1 || floor.Normal.z>.7071f) &&
           length(raised-desired)<length(resolved-desired))resolved=raised;
    }
    if(hit.Fraction<1 && hit.Actor && !hit.Actor->IsA("Pawn") && std::abs(delta.z)<.5f && std::abs(hit.Normal.z)<.2f && length(delta)<160.f) {
        // Reconciliation endpoints can lie around a corner: the straight
        // chord is blocked even though normal walking has gone along the wall.
        // Follow bounded collision sweeps along the wall instead of pinning the
        // visual pawn until the continuity limit forces a snap. No VM movement,
        // actor pushes, touching or gameplay state changes are performed here.
        auto at=origin,remaining=delta;
        for(int iteration=0;iteration<4 && length(remaining)>.001f;++iteration) {
            auto contact=sweep(at,remaining);
            at+=remaining*contact.Fraction;
            if(contact.Fraction>=1)break;
            if(std::abs(contact.Normal.z)>.2f || length(contact.Normal)<.5f)break;
            remaining*=1-contact.Fraction;
            auto slide=remaining-contact.Normal*std::min(0.f,dot(remaining,contact.Normal));
            if(dot(slide,delta)<=0 || length(slide-remaining)<.0001f)break;
            remaining=slide;
        }
        // A lateral detour must still have floor support at the same height;
        // otherwise it can strand the presentation beside a stair or lift.
        auto support=sweep(at,vec3(0,0,-2.f));
        if(support.Fraction<1 && support.Normal.z>.7071f &&
           length(at-desired)<length(resolved-desired))resolved=at;
    }
    player->Location()=resolved;
    for(int axis=0;axis<3;++axis)player->Velocity()[axis]=pose.velocity[axis];
    player->Region()=player->FindRegion();player->FootRegion()=player->FindRegion(vec3(0,0,-player->CollisionHeight()));
    player->HeadRegion()=player->FindRegion(vec3(0,0,player->EyeHeight()));
}
