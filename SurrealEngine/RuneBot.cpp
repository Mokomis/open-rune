#include "RuneBattleLaws.h"
#include "RuneSpider.h"
#include "Precomp.h"
#include "RuneFinisher.h"
#include "RuneCorpse.h"
#include "RuneSlam.h"
#include "RuneBot.h"
#include "Utils/CommandLine.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneTacticsPolicy.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneDualWield.h"
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include <map>
#include <queue>
#include <iostream>
#include <chrono>
#include <unordered_map>
namespace RuneBot {
bool ClassicRulesRequested(){return commandline->GetArg("","--rune-bot-brain","")=="classic";}
namespace {
// Guard long enough to cover an opponent's whole measured swing.
uint64_t guardHold(const WeaponProfile& w){return w.valid?uint64_t(std::clamp(w.windup+w.duration*.5f+.15f,.4f,.8f)*1000):450;}
mat4 affineInverse(const mat4& m) {
    auto r=mat3::inverse(mat3(m));auto out=mat4::identity();
    for(int c=0;c<3;++c)for(int row=0;row<3;++row)out[c*4+row]=r[c*3+row];
    auto t=r*(-vec3(m[12],m[13],m[14]));out[12]=t.x;out[13]=t.y;out[14]=t.z;return out;
}
}
// Measured from the weapon's own blade (SweepJoint1..SweepJoint2 + ExtendedLength)
// carried through this pawn's authored attack animations. Cached per model/weapon.
WeaponProfile ProfileWeapon(UPawn* p,UActor* weapon)
{
    static std::map<std::pair<const void*,const void*>,WeaponProfile> cache;
    if(!p || !weapon || !weapon->IsA("Weapon"))return {};
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    auto* blade=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(weapon));
    if(!model || !blade)return {};
    auto key=std::make_pair(static_cast<const void*>(model),static_cast<const void*>(weapon->Class));
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    WeaponProfile out;
    const int j1=weapon->GetInt("SweepJoint1"),j2=weapon->GetInt("SweepJoint2");
    const int attach=weapon->GetByte("AttachParentJoint");
    auto weaponBones=blade->ActorJointMatrices(weapon);
    if(j1<0 || j2<0 || size_t(j1)>=weaponBones.size() || size_t(j2)>=weaponBones.size() || attach<0 || size_t(attach)>=model->Joints.size()){cache[key]=out;return out;}
    const auto pawnWorld=GetRuneMeshToWorld(p);
    const auto liveGrip=RuneDualWield::Attachment(*model,p,weapon,model->ActorJointMatrices(p));
    // Weapon mesh relative to its grip; constant while the hand animates.
    const auto offset=affineInverse(pawnWorld*liveGrip)*GetRuneMeshToWorld(weapon);
    vec3 base=(weaponBones[j1]*vec4(0,0,0,1)).xyz(),tip=(weaponBones[j2]*vec4(0,0,0,1)).xyz();
    const float extended=weapon->GetFloat("ExtendedLength");
    const auto origin=p->Location();const float yaw=p->Rotation().YawRadians();
    const vec3 forward(std::cos(yaw),std::sin(yaw),0),side(-std::sin(yaw),std::cos(yaw),0);
    std::vector<float> reaches,windups,durations;
    for(const char* field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackStandA","A_AttackStandB"}) {
        if(!weapon->HasProperty(field))continue;
        auto name=weapon->GetName(field);if(name.IsNone())continue;
        int sequence=-1;
        for(size_t s=0;s<model->Sequences.size();++s)if(model->Sequences[s].Info.Name==name){sequence=int(s);break;}
        if(sequence<0)continue;
        const auto& info=model->Sequences[sequence].Info;
        const float frames=float(std::max(1,info.NumFrames)),seconds=frames/std::max(1.f,info.Rate);
        std::vector<float> trace;float best=0;
        for(int sample=0;sample<=30;++sample) {
            // SampleFrame takes a 0..1 phase through the sequence.
            auto bones=model->JointMatrices(model->SampleFrame(size_t(sequence),float(sample)/30.f,false));
            const auto toWorld=pawnWorld*RuneDualWield::Attachment(*model,p,weapon,bones)*offset;
            const auto worldBase=(toWorld*vec4(base,1)).xyz();auto worldTip=(toWorld*vec4(tip,1)).xyz();
            if(extended>0 && length(worldTip-worldBase)>.01f)worldTip+=normalize(worldTip-worldBase)*extended;
            float reach=0;
            for(int step=0;step<=4;++step) {
                const auto d=worldBase+(worldTip-worldBase)*(float(step)/4)-origin;
                if(std::abs(dot(d,side))<=24.f && std::abs(d.z)<64.f)reach=std::max(reach,dot(d,forward));
            }
            trace.push_back(reach);best=std::max(best,reach);
        }
        if(best<=0)continue;
        int first=0;while(first<int(trace.size()) && trace[first]<best*.85f)++first;
        reaches.push_back(best);windups.push_back(seconds*float(first)/30.f);durations.push_back(seconds);
    }
    if(!reaches.empty()) {
        auto median=[](std::vector<float> v){std::sort(v.begin(),v.end());return v[v.size()/2];};
        out.reach=median(reaches);out.windup=median(windups);out.duration=median(durations);out.valid=true;
    }
    std::cout<<"OPEN_RUNE_BOT_WEAPON pawn="<<p->Class->Name.ToString()<<" weapon="<<weapon->Class->Name.ToString()<<" valid="<<out.valid
        <<" reach="<<out.reach<<" windup="<<out.windup<<" duration="<<out.duration<<" sequences="<<reaches.size()<<std::endl;
    cache[key]=out;return out;
}
using namespace openrune::net;
namespace {
vec3 position(const Entity& e){return vec3(float(e.position[0]),float(e.position[1]),float(e.position[2]))*.125f;}
float flat(vec3 d){d.z=0;return length(d);}
vec3 direction(vec3 d){d.z=0;return length(d)>.001f?normalize(d):vec3(0);}
const Entity* find(const World& w,EntityRef ref){for(const auto& e:w)if(e.ref==ref)return &e;return nullptr;}
uint8_t team(const MatchStatus& s,EntityRef ref){for(const auto& p:s.scores)if(p.pawn==ref)return p.team;return 255;}
bool enemy(const MatchStatus& s,EntityRef own,EntityRef other){
    if(own==other)return false;
    if(s.mode==GameMode::Deathmatch)return true;
    auto a=team(s,own),b=team(s,other);return a<4 && b<4 && a!=b;
}
bool swinging(UActor* actor)
{
    auto* pawn=UObject::TryCast<UPawn>(actor);
    if(!pawn)return false;
    const auto body=pawn->AnimSequence();
    auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
    const auto torso=proxy?proxy->AnimSequence():NameString("None");
    // Rune weapon sequences are often abbreviated (S1_..., ATK_...), so
    // recognize the actual authored attack fields rather than guessing from
    // the word "attack" in an animation's name. Recovery fields are excluded.
    for(auto* weapon:{pawn->Weapon(),pawn->Dual.offhand})if(weapon)for(const char* field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD",
        "A_AttackStandA","A_AttackStandB","A_AttackBackupA","A_AttackBackupB","A_AttackStrafeLeft","A_AttackStrafeRight","A_JumpAttack"}) {
        if(!weapon->HasProperty(field))continue;
        auto animation=weapon->GetName(field);
        if(!animation.IsNone() && (animation==body || animation==torso))return true;
    }
    std::string name=body.ToString()+" "+torso.ToString();
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    return name.find("kick")!=std::string::npos || name.find("bash")!=std::string::npos;
}
// Visible animation phase [0,1] of an active weapon swing, or -1 if not swinging a weapon.
float swingPhase(UActor* actor)
{
    auto* pawn=UObject::TryCast<UPawn>(actor);
    if(!pawn)return -1.f;
    const auto body=pawn->AnimSequence();
    auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
    const auto torso=proxy?proxy->AnimSequence():NameString("None");
    for(auto* weapon:{pawn->Weapon(),pawn->Dual.offhand})if(weapon)for(const char* field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD",
        "A_AttackStandA","A_AttackStandB","A_AttackBackupA","A_AttackBackupB","A_AttackStrafeLeft","A_AttackStrafeRight","A_JumpAttack"}) {
        if(!weapon->HasProperty(field))continue;
        auto animation=weapon->GetName(field);
        if(!animation.IsNone()) {
            if(proxy && animation==torso)return std::clamp(proxy->AnimFrame(),0.f,1.f);
            if(animation==body)return std::clamp(pawn->AnimFrame(),0.f,1.f);
        }
    }
    return -1.f;
}
// True while our own visible AnimProxy is still executing an attack or return animation,
// so we do not waste an Attack action and lock out readyAt while the weapon is busy.
bool ownAttackBusy(UPawn* pawn)
{
    if(!pawn)return false;
    auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
    if(!proxy)return false;
    const auto torso=proxy->AnimSequence();
    for(auto* weapon:{pawn->Weapon(),pawn->Dual.offhand})if(weapon)for(const char* field:{
        "A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackStandA","A_AttackStandB",
        "A_AttackBackupA","A_AttackBackupB","A_AttackStrafeLeft","A_AttackStrafeRight","A_JumpAttack",
        "A_AttackAReturn","A_AttackBReturn","A_AttackCReturn","A_AttackDReturn",
        "A_AttackStandAReturn","A_AttackStandBReturn","A_AttackBackupAReturn","A_AttackBackupBReturn"}) {
        if(!weapon->HasProperty(field))continue;
        auto animation=weapon->GetName(field);
        if(!animation.IsNone() && animation==torso)return true;
    }
    return false;
}
static bool guardBreaker(UActor* actor)
{
    auto* pawn=UObject::TryCast<UPawn>(actor);
    if(!pawn)return false;
    auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
    std::string name=pawn->AnimSequence().ToString()+" "+(proxy?proxy->AnimSequence().ToString():std::string());
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    return name.find("kick")!=std::string::npos || name.find("bash")!=std::string::npos;
}
static bool kicking(UActor* actor)
{
    auto* pawn=UObject::TryCast<UPawn>(actor);
    if(!pawn)return false;
    auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
    std::string name=pawn->AnimSequence().ToString()+" "+(proxy?proxy->AnimSequence().ToString():std::string());
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    return name.find("kick")!=std::string::npos;
}
}
// Match walking collision, including solid scenery actors. Actor.Trace with
// traceActors=false includes BSP/movers but omits blocking mesh decorations.
// Pawns are handled dynamically by combat/avoidance, not baked into routes.
static UActor* terrainTrace(UPlayerPawn* p,vec3& hitLocation,vec3& hitNormal,const vec3& to,const vec3& from,const vec3& extent)
{
    for(const auto& hit:p->XLevel()->Collision.Trace(from,to,extent.z,extent.x,true,true,false)) {
        auto* actor=hit.Actor;
        if(!actor || actor==p || actor->IsOwnedBy(p))continue;
        if(actor==p->Level() || (!actor->IsA("Pawn") && actor->bBlockPlayers())) {
            hitLocation=from+(to-from)*hit.Fraction;hitNormal=hit.Normal;return actor;
        }
    }
    hitLocation=to;hitNormal=vec3(0);return nullptr;
}
bool Brain::safe(UPlayerPawn* p,vec3 from,vec3 to){
    auto* z=p->Level()->GetLocZone(to,{}).Zone;
    if(!z || z->bPainZone() || z->bWaterZone() || z->IsA("HeadBallZone"))return false;
    vec3 hit,normal;
    const vec3 extent(p->CollisionRadius(),p->CollisionRadius(),std::max(1.f,p->CollisionHeight()-1.f));
    const vec3 rise(0,0,std::clamp(p->MaxStepHeight(),0.f,48.f));
    if(terrainTrace(p,hit,normal,to,from,extent)) {
        // A walking stair is not a wall. Test the same bounded step-up that
        // ordinary player movement can perform; this never moves the pawn.
        if(terrainTrace(p,hit,normal,from+rise,from,extent) ||
           terrainTrace(p,hit,normal,to+rise,from+rise,extent))return false;
    }
    if(!terrainTrace(p,hit,normal,to-vec3(0,0,p->CollisionHeight()+64),to+rise,vec3(8,8,0)) || normal.z<.65f)return false;
    auto* landing=p->Level()->GetLocZone(hit+vec3(0,0,4),{}).Zone;
    return landing && !landing->bPainZone() && !landing->bWaterZone() && !landing->IsA("HeadBallZone");
}
// Reciprocal traffic uses only nearby visible teammates and their observed
// velocity. No hidden actors, input reads, collision exemptions or teleports.
void Brain::avoidTeammates(UPlayerPawn* p,RuneNetworkReplica& replica,const World& world,const MatchStatus& status,const Entity& self,uint64_t now,Decision& out)
{
    const auto ownTeam=team(status,self.ref);
    if(status.mode==GameMode::Deathmatch || ownTeam>=4 || status.phase!=MatchPhase::Playing || self.health<=0 ||
        !(self.flags&2) || self.combat.phase || (out.action && (*out.action==ActionKind::Jump || *out.action==ActionKind::BloodLeap || *out.action==ActionKind::ShieldCharge)))return;
    float magnitude=std::hypot(float(out.input.forward),float(out.input.strafe));
    if(magnitude<100 || (liftActive>=0 && liftPhase>=2))return;
    float yaw=float(out.input.yaw)*6.2831853f/65536;vec3 facing(std::cos(yaw),std::sin(yaw),0),right(-facing.y,facing.x,0);
    auto wanted=direction(facing*float(out.input.forward)+right*float(out.input.strafe));auto here=position(self);
    struct Neighbor {const Entity* state;vec3 at,velocity;float radius;};std::vector<Neighbor> nearby;
    const Entity* blocker=nullptr;float best=1e9f;
    // Use observed walking speed, not nominal GroundSpeed: Rune applies
    // movement-mode modifiers. A convoy already moving away is not a blockage.
    float observedSpeed=std::max(70.f,std::hypot(float(self.velocity[0])*.25f,float(self.velocity[1])*.25f));
    for(const auto& e:world)if(e.kind==EntityKind::Pawn && e.ref!=self.ref && e.health>0 && !(e.flags&32) && team(status,e.ref)==ownTeam){
        auto at=position(e);auto offset=at-here;
        if(std::abs(offset.z)>60 || flat(offset)>220 || !p->FastTrace(at,here))continue;
        auto* other=UObject::TryCast<UPawn>(replica.actor(e.ref));float radius=p->CollisionRadius()+(other?other->CollisionRadius():18.f)+5.f;
        vec3 velocity(float(e.velocity[0])*.25f,float(e.velocity[1])*.25f,0);nearby.push_back({&e,at,velocity,radius});
        float along=dot(offset,wanted),lateral=flat(offset-wanted*along);
        if(along>-radius*.35f && along<radius+85 && lateral<radius+9){
            auto relative=(wanted*observedSpeed-velocity)*.35f;auto start=-offset;start.z=0;
            float t=dot(relative,relative)>.001f?std::clamp(-dot(start,relative)/dot(relative,relative),0.f,1.f):0;
            if(flat(start)>radius+9 && flat(start+relative*t)>radius+9)continue;
            float score=std::max(0.f,along)+lateral;if(score<best){best=score;blocker=&e;}
        }
    }
    if(!blocker){trafficBlocker={};trafficUntil=0;return;}
    const bool yielding=self.ref.id>blocker->ref.id;
    auto clearStep=[&](vec3 heading,float speed){
        auto step=heading*(observedSpeed*speed/850.f*.25f);
        for(const auto& n:nearby){auto start=here-n.at,relative=step-n.velocity*.25f;start.z=relative.z=0;
            float t=dot(relative,relative)>.001f?std::clamp(-dot(start,relative)/dot(relative,relative),0.f,1.f):0;
            float initial=flat(start),closest=flat(start+relative*t),end=flat(start+relative);
            if(initial<=n.radius+1){if(end<initial+.25f)return false;}
            else if(closest<n.radius)return false;
        }return true;
    };
    auto steer=[&](vec3 goal,float speed){auto heading=direction(goal-here);
        out.input.forward=int16_t(dot(heading,facing)*speed);out.input.strafe=int16_t(dot(heading,right)*speed);
    };
    float otherYaw=float(blocker->angles[1])*6.2831853f/65536;
    bool following=dot(wanted,vec3(std::cos(otherYaw),std::sin(otherYaw),0))>.6f;
    if(following && dot(position(*blocker)-here,wanted)>0){
        // Queue behind a teammate moving the same way instead of constantly
        // overtaking them. A stationary friend gets only a brief grace period.
        for(float speed:{std::min(850.f,magnitude),650.f,450.f,250.f})if(speed<=magnitude && clearStep(wanted,speed) && safe(p,here,here+wanted*16.f)){
            trafficBlocker=blocker->ref;trafficUntil=0;trafficWaitUntil=0;steer(here+wanted*64.f,speed);return;
        }
        if(trafficBlocker!=blocker->ref)trafficWaitUntil=now+450;
        if(now<trafficWaitUntil){trafficBlocker=blocker->ref;out.input.forward=out.input.strafe=0;return;}
    }
    if(trafficBlocker==blocker->ref && now<trafficUntil && flat(trafficGoal-here)>12){
        auto heading=direction(trafficGoal-here);
        if(safe(p,here,here+heading*16.f) && clearStep(heading,450))steer(trafficGoal,450);
        else out.input.forward=out.input.strafe=0; // Commit briefly; do not flip sides every think.
        return;
    }
    trafficBlocker=blocker->ref;trafficUntil=0;
    auto offset=position(*blocker)-here;float along=dot(offset,wanted);
    const auto tangent=vec3(wanted.y,-wanted.x,0); // Keep to own right when meeting head-on.
    float width=p->CollisionRadius()*2+28;
    float advance=yielding&&!following?-32.f:std::clamp(along-p->CollisionRadius()*2-12.f,0.f,32.f);
    std::array<vec3,4> candidates={here+tangent*width+wanted*advance,here-tangent*width+wanted*advance,
        here-wanted*96.f+tangent*24.f,here-wanted*96.f};
    for(size_t i=0;i<candidates.size();++i){
        if(i>=2 && !yielding)continue;auto goal=candidates[i];auto heading=direction(goal-here);
        if(!safe(p,here,goal) || !clearStep(heading,450))continue;
        bool occupied=false;for(const auto& n:nearby)if(flat(goal-n.at)<n.radius+8)occupied=true;
        if(occupied)continue;
        trafficGoal=goal;trafficUntil=now+1000;steer(goal,450);return;
    }
    // One stable right-of-way decision in a doorway, not two mirrored dances.
    // The higher actor ID gives room first; the lower follows once clear.
    if(!yielding && clearStep(wanted,500) && safe(p,here,here+wanted*16.f))steer(here+wanted*64.f,500);
    else out.input.forward=out.input.strafe=0;
}
bool Brain::walkableLine(UPlayerPawn* p,vec3 from,vec3 to)
{
    if(flat(to-from)>800)return false;
    auto at=from;int steps=std::max(1,int(std::ceil(flat(to-from)/16.f)));
    for(int k=1;k<=steps;++k) {
        auto sample=from+(to-from)*(float(k)/steps);sample.z=at.z;
        vec3 hit,normal;
        if(!terrainTrace(p,hit,normal,sample-vec3(0,0,p->CollisionHeight()+64),sample+vec3(0,0,p->MaxStepHeight()),vec3(4,4,0)) || normal.z<.65f)return false;
        auto standing=hit+vec3(0,0,p->CollisionHeight()+1);
        if(standing.z-at.z>p->MaxStepHeight()+1 || at.z-standing.z>64 || !safe(p,at,standing))return false;
        at=standing;
    }
    return std::abs(at.z-to.z)<32;
}
std::vector<vec3> Brain::localPath(UPlayerPawn* p,vec3 origin,vec3 goal,float arrivalRadius,float arrivalHeight)
{
    // Bounded floor search bridges missing map reachspecs. Every edge uses the
    // same collision, step-height and safe-landing tests as ordinary movement.
    struct Node { vec3 point; int x,y,parent; float cost; };
    std::vector<Node> nodes{{origin,0,0,-1,0}};
    std::unordered_map<uint64_t,int> found;
    // Keep stacked floors distinct: a staircase can return to the same XY
    // cell at a different height.
    auto key=[&](int x,int y,float z){
        auto layer=int(std::floor((z-origin.z)/48.f));
        return (uint64_t(uint32_t(x)&0x1fffff)<<42) | (uint64_t(uint32_t(y)&0x1fffff)<<21) | uint32_t(layer)&0x1fffff;
    };
    found[key(0,0,origin.z)]=0;
    using Entry=std::pair<float,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    open.push({flat(goal-origin),0});
    int end=-1,expanded=0;
    while(!open.empty() && expanded++<1536 && nodes.size()<4096)
    {
        auto [estimate,i]=open.top();open.pop();
        const auto current=nodes[i];
        if(estimate>current.cost+flat(goal-current.point)+.01f)continue;
        if(flat(goal-current.point)<arrivalRadius && std::abs(goal.z-current.point.z)<arrivalHeight){end=i;break;}
        for(auto offset:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}})
        {
            int x=current.x+offset.first,y=current.y+offset.second;
            if(std::abs(x)>40 || std::abs(y)>40)continue;
            auto location=current.point;bool traversable=true;
            for(int part=1;part<=3;++part) {
                auto sample=vec3(current.point.x+offset.first*16.f*part,current.point.y+offset.second*16.f*part,location.z);
                vec3 hit,normal;
                if(!terrainTrace(p,hit,normal,sample-vec3(0,0,p->CollisionHeight()+64),sample+vec3(0,0,p->MaxStepHeight()),vec3(4,4,0)) || normal.z<.65f){traversable=false;break;}
                auto standing=hit+vec3(0,0,p->CollisionHeight()+1);
                if(standing.z-location.z>p->MaxStepHeight()+1 || location.z-standing.z>64 || !safe(p,location,standing)){traversable=false;break;}
                location=standing;
            }
            if(!traversable)continue;
            float cost=current.cost+48.f+std::abs(location.z-current.point.z)*.3f;
            auto known=found.find(key(x,y,location.z));int next;
            if(known!=found.end()) {
                next=known->second;
                if(std::abs(nodes[next].point.z-location.z)>64 || nodes[next].cost<=cost)continue;
                nodes[next]={location,x,y,i,cost};
            } else {next=int(nodes.size());found[key(x,y,location.z)]=next;nodes.push_back({location,x,y,i,cost});}
            open.push({cost+flat(goal-location),next});
        }
    }
    std::vector<vec3> result;
    if(end>=0){for(int i=end;i>=0;i=nodes[i].parent)result.push_back(nodes[i].point);std::reverse(result.begin(),result.end());}
    if(debug && end<0){float nearest=1e9f;vec3 closest;
        for(const auto& n:nodes)if(length(n.point-goal)<nearest){nearest=length(n.point-goal);closest=n.point;}
        std::cout<<"OPEN_RUNE_AI_LOCAL_NEAREST distance="<<nearest<<" pos="<<closest.x<<","<<closest.y<<","<<closest.z<<std::endl;
    }
    if(debug)std::cout<<"OPEN_RUNE_AI_LOCAL expanded="<<expanded<<" points="<<result.size()<<" goal="<<goal.x<<","<<goal.y<<","<<goal.z<<std::endl;
    return result;
}
bool Brain::rejectedAt(vec3 point,uint64_t now){
    std::erase_if(rejected,[&](const auto& r){return now>=r.until;});
    return std::any_of(rejected.begin(),rejected.end(),[&](const auto& r){return length(r.point-point)<100;});
}
void Brain::rejectGoal(vec3 point,uint64_t now){
    std::erase_if(rejected,[&](const auto& r){return now>=r.until || length(r.point-point)<100;});
    if(rejected.size()>=8)rejected.erase(rejected.begin());
    rejected.push_back({point,now+8000});
}
vec3 Brain::navigate(Engine& game, UPlayerPawn* p, vec3 goal, uint64_t now)
{
    const auto here = p->Location();
    if (flat(goal-here)<36 && std::abs(goal.z-here.z)<80) return here;
    if (flat(goal-here)<160 && std::abs(goal.z-here.z)<60 && p->FastTrace(goal,here) && walkableLine(p,here,goal)) return goal;
    if (now>=routeAt || length(goal-waypoint)>180)
    {
        if(length(goal-waypoint)>180)unreachableAt=0;
        routeAt=now+4000; partialRoute=false;waypoint=goal; route.clear(); routeIndex=0;
        if(navPoints.empty()) {
        std::vector<UNavigationPoint*> nodes;
        std::unordered_map<UNavigationPoint*,int> index;
        for (auto* actor:game.Level->Actors)
            if (auto* n=UObject::TryCast<UNavigationPoint>(actor); n && !n->bDeleteMe() && nodes.size()<2048)
            { index[n]=int(nodes.size()); nodes.push_back(n); }
        std::vector<vec3> points;
        for (auto* node:nodes)
        {
            // Rune's editor starts and inventory markers can float above the
            // floor. Reach the standing position, not their icon in mid-air.
            vec3 at=node->Location(),hit,normal;
            if (terrainTrace(p,hit,normal,at-vec3(0,0,1024),at,vec3(4,4,0)) && normal.z>.65f && at.z-hit.z>p->CollisionHeight()*.5f)
                at=hit+vec3(0,0,p->CollisionHeight()+1);
            points.push_back(at);
            navRadius.push_back(node->IsA("Teleporter")?std::max(32.f,node->CollisionRadius()+p->CollisionRadius()-8):64.f);
            navHeight.push_back(node->IsA("Teleporter")?node->CollisionHeight()+p->CollisionHeight()-8:80.f);
        }
        std::vector<std::vector<Link>> links(nodes.size());
        for (const auto& edge:game.Level->ReachSpecs)
        {
            auto from=index.find(edge.startActor),to=index.find(edge.endActor);
            if (from==index.end() || to==index.end() || edge.bPruned ||
                edge.collisionRadius<p->CollisionRadius() || edge.collisionHeight<p->CollisionHeight()) continue;
            if (edge.reachFlags&(R_FLY|R_SWIM)) continue;
            if (edge.reachFlags&R_SPECIAL)
            {
                // Follow authored teleporters by walking into them. Only the
                // server's original Touch script can actually teleport us.
                if (!edge.startActor->IsA("Teleporter") || !edge.endActor->IsA("Teleporter")) continue;
            }
            else if (!(edge.reachFlags&R_WALK)) continue;
            auto* zone=game.LevelInfo->GetLocZone(points[to->second],{}).Zone;
            if (!zone || zone->bPainZone() || zone->bWaterZone() || zone->IsA("HeadBallZone")) continue;
            links[from->second].push_back({to->second,float(std::max(1,edge.distance))+((edge.reachFlags&R_JUMP)?100.f:0.f)});
        }
        // Some multiplayer maps never connected their spectator/player starts
        // to the authored graph. Infer only short, swept walking links, once
        // per map, including disconnected subgraphs that already have internal
        // links. A fixed floor-sample budget bounds this one-time work. Never
        // fabricate a teleport, jump or door activation here.
        int floorSamples=0;
        for(int i=0;i<int(nodes.size()) && floorSamples<8192;++i) {
            std::vector<std::pair<float,int>> neighbors;
            for(int j=0;j<int(nodes.size());++j)if(i!=j && length(points[j]-points[i])<800 && std::abs(points[j].z-points[i].z)<400)
                neighbors.push_back({length(points[j]-points[i]),j});
            std::sort(neighbors.begin(),neighbors.end());
            if(neighbors.size()>8)neighbors.resize(8);
            for(auto [distance,j]:neighbors) {
                if(std::any_of(links[i].begin(),links[i].end(),[&](const Link& l){return l.to==j;}))continue;
                auto at=points[i];bool walkable=true;int steps=std::max(1,int(std::ceil(flat(points[j]-at)/16.f)));
                for(int k=1;k<=steps;++k) {
                    if(++floorSamples>8192){walkable=false;break;}
                    auto xy=points[i]+(points[j]-points[i])*(float(k)/steps);xy.z=at.z;
                    vec3 hit,normal;
                    if(!terrainTrace(p,hit,normal,xy-vec3(0,0,p->CollisionHeight()+64),xy+vec3(0,0,p->MaxStepHeight()),vec3(4,4,0)) || normal.z<.65f){walkable=false;break;}
                    auto next=hit+vec3(0,0,p->CollisionHeight()+1);
                    if(next.z-at.z>p->MaxStepHeight()+1 || at.z-next.z>64 || !safe(p,at,next)){walkable=false;break;}
                    at=next;
                }
                if(walkable && std::abs(at.z-points[j].z)<32)links[i].push_back({j,distance});
            }
        }
        if(debug)for(auto* node:nodes)if(node->IsA("Teleporter"))std::cout<<"OPEN_RUNE_AI_PORTAL pos="<<node->Location().x<<","<<node->Location().y<<","<<node->Location().z<<" radius="<<node->CollisionRadius()<<" height="<<node->CollisionHeight()<<" enabled="<<node->GetBool("bEnabled")<<" url="<<node->GetString("URL")<<std::endl;
        navPoints=std::move(points);navLinks=std::move(links);
        }
        const auto& points=navPoints;const auto& links=navLinks;
        const auto count=points.size();
        std::vector<float> costs(count,1e20f);
        std::vector<int> previous(count,-1);
        using Entry=std::pair<float,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
        // A visible node across a pit is not a walking entrance. Validate the
        // entire ground connection before seeding graph search; otherwise a
        // one-node "route" continually sends the bot straight into the gap.
        for (int i=0;i<int(count);++i)
        {
            float distance=length(points[i]-here);
            if (!links[i].empty() && distance<512 && std::abs(points[i].z-here.z)<180 && p->FastTrace(points[i],here) && walkableLine(p,here,points[i]))
            { costs[i]=distance; open.push({distance,i}); }
        }
        int visited=0;
        while (!open.empty() && visited++<4096)
        {
            auto [cost,i]=open.top(); open.pop();
            if (cost!=costs[i]) continue;
            for (const auto& link:links[i])
            {
                float next=cost+link.cost;
                if (next<costs[link.to])
                { costs[link.to]=next; previous[link.to]=i; open.push({next,link.to}); }
            }
        }
        int end=-1; float closest=1e20f;
        for (int i=0;i<int(count);++i)
            if (costs[i]<1e19f)
            {
                float score=length(points[i]-goal)+costs[i]*.05f;
                if (score<closest) { closest=score; end=i; }
            }
        if (end>=0)
        {
            for (int i=end;i>=0;i=previous[i]) route.push_back(points[i]);
            std::reverse(route.begin(),route.end()); ++routes;
        }
        if(route.size()<=1 && closest>120) {
            int goalNode=-1;float nearestGoal=1e20f;
            for(int i=0;i<int(count);++i)if(length(points[i]-goal)<nearestGoal){nearestGoal=length(points[i]-goal);goalNode=i;}
            std::vector<bool> leadsToGoal(count,false);
            if(goalNode>=0)leadsToGoal[goalNode]=true;
            for(size_t pass=0;pass<count;++pass){bool changed=false;
                for(int i=0;i<int(count);++i)if(!leadsToGoal[i])for(const auto& link:links[i])if(leadsToGoal[link.to]){leadsToGoal[i]=true;changed=true;break;}
                if(!changed)break;
            }
            int bridge=-1;float best=1e20f;
            for(int i=0;i<int(count);++i)if(costs[i]>=1e19f && leadsToGoal[i] && !links[i].empty()) {
                float score=length(points[i]-here)+length(points[i]-goal)*.1f;
                if(score<best){best=score;bridge=i;}
            }
            const int destination=bridge>=0?bridge:goalNode;
            if(destination>=0) {
                auto connection=localPath(p,here,points[destination],navRadius[destination],navHeight[destination]);
                if(!connection.empty()){route=std::move(connection);routeAt=now+8000;}
                else {
                    // A nearest node in the wrong component is not a route.
                    // Do not spend a whole patrol interval circling its wall.
                    routeAt=now+3000;partialRoute=true;
                }
            }
        }
        if (debug) std::cout<<"OPEN_RUNE_AI_PATH nodes="<<count<<" reachable="<<visited<<" route="<<route.size()<<std::endl;
    }
    while (routeIndex<route.size() && flat(route[routeIndex]-here)<24 && std::abs(route[routeIndex].z-here.z)<64) ++routeIndex;
    if(routeIndex>=route.size() && length(goal-here)>120 && !walkableLine(p,here,goal))partialRoute=true;
    if(partialRoute){
        // Give ordinary jumps, ledge grabs and door Use a chance before
        // rejecting a missing graph connection. Stopping immediately here
        // prevents the movement recovery below from ever running.
        if(!unreachableAt)unreachableAt=now;
        else if(now-unreachableAt>4000){rejectGoal(goal,now);patrolAt=routeAt=unreachableAt=0;route.clear();return here;}
    }else unreachableAt=0;
    auto result=routeIndex<route.size()?route[routeIndex]:goal;
    if(debug && now>=debugAt)std::cout<<"OPEN_RUNE_AI_NEXT pos="<<result.x<<","<<result.y<<","<<result.z<<" delta="<<length(result-here)<<" zone="<<(p->Region().Zone?p->Region().Zone->Class->Name.ToString():"None")<<std::endl;
    return result;
}
Decision Brain::think(Engine& game,RuneNetworkReplica& replica,const World& world,const MatchStatus& status,EntityRef own,uint64_t now){
    const auto* self=find(world,own);auto* p=UObject::TryCast<UPlayerPawn>(replica.actor(own));
    if(!self || !p)return {};
    if(life!=self->life){spiderAt=spiderRetry=0;ledgeSince=ledgeJumpAt=0;supplyTarget={};supplyTries=0;liftActive=-1;liftRetryAt=0;slamAt=slamCooldown=0;slamStage=0;swingWindupUntil=0;life=self->life;lastHealth=self->health;searchUntil=0;equipmentUntil=now+25000;target={};nextThink=readyAt=guardUntil=kickAt=bashAt=useAt=routeAt=patrolAt=progressAt=jumpAt=woundedAt=leapAt=jumpUntil=unreachableAt=supportUntil=guardAt=powerAt=escapeUntil=trafficUntil=trafficWaitUntil=0;trafficBlocker={};seenAt=lastSeen=threatAt=blockedAt=0;wasThreat=false;route.clear();rejected.clear();searchedPatrols.clear();intent={};side=random()<.5f?-1:1;}
    if(self->health>0 && p->AnimSequence()==RuneShieldTaunt::Clip){Decision out;out.input.life=life;out.input.yaw=self->angles[1];intent=out;return out;}
    if(now<nextThink){auto out=intent;out.action.reset();out.input.life=life;return out;}
    const auto decisionStarted=std::chrono::steady_clock::now();
    nextThink=now+100;++decisions;Decision out;out.input.life=life;out.input.yaw=self->angles[1];
    auto finish=[&](){
        if(debug && now>=debugAt){debugAt=now+3000;
            if(status.mode==GameMode::HeadBall) {
                int heads=0,visible=0;float nearest=1e9f;
                for(const auto& item:world)if(!item.owner.id && !(item.flags&32))if(auto* a=replica.actor(item.ref);a && a->IsA("Head")) {
                    ++heads;float d=length(position(item)-p->Location());nearest=std::min(nearest,d);
                    if(d<700 && p->FastTrace(position(item),p->Location()))++visible;
                }
                std::cout<<"OPEN_RUNE_AI_HEADS free="<<heads<<" visible="<<visible<<" nearest="<<nearest<<" held="<<(p->Weapon()?p->Weapon()->Class->Name.ToString():"None")<<std::endl;
            }
            std::cout<<"OPEN_RUNE_AI_TRACE team="<<int(team(status,own))<<" yaw="<<self->angles[1]<<" pawn="<<own.id<<" pos="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" health="<<self->health<<" weapon="<<self->weapon.id<<" target="<<target.id<<" route="<<routeIndex<<"/"<<route.size()<<" goal="<<waypoint.x<<","<<waypoint.y<<","<<waypoint.z<<" move="<<out.input.forward<<","<<out.input.strafe<<" action="<<(out.action?int(*out.action):0)<<std::endl;}
        if(!(self->combat.flags&16))avoidTeammates(p,replica,world,status,*self,now,out);
        if(debug && debugAt==now+3000)std::cout<<"OPEN_RUNE_AI_TRAFFIC block="<<trafficBlocker.id<<" until="<<trafficUntil<<" now="<<now<<" goal="<<trafficGoal.x<<","<<trafficGoal.y<<","<<trafficGoal.z<<" move="<<out.input.forward<<","<<out.input.strafe<<std::endl;
        maxThinkUs=std::max(maxThinkUs,uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-decisionStarted).count()));
        intent=out;return out;
    };
    if(status.phase==MatchPhase::Playing && self->health>0 && p->finisherHeld && game.TotalTime-p->finisherPoseStart>.70 && now>=readyAt){
        out.action=ActionKind::Attack;readyAt=now+450;return finish();
    }
    auto act=[&](ActionKind a,uint64_t recovery){if(now>=readyAt){out.action=a;readyAt=now+recovery;return true;}return false;};
    if(self->physical.kind==1 || self->physical.kind==3){liftActive=-1;slamStage=0;intent=out;return out;}
    if(status.phase!=MatchPhase::Playing)return finish();
    if(self->combat.flags&16){out.input.forward=1000;out.input.yaw=self->angles[1];
        if(!RuneBattleLaws::Allowed(RuneBattleLaws::Spider) || now>spiderRetry+10000){act(ActionKind::Use,500);spiderRetry=now+3000;}
        return finish();}
    if(spiderAt){if(!RuneBattleLaws::Allowed(RuneBattleLaws::Spider)||now>spiderAt+500){spiderAt=0;return finish();}
        out.input.yaw=uint16_t(Rotator::FromVector(-spiderNormal).Yaw);out.input.forward=0;
        if(now>=spiderAt && act(ActionKind::Use,400)){spiderAt=0;spiderRetry=now+2000;}return finish();}

    // Observe our own replicated climbing pose, not server-only state. Give
    // the authored automatic pull-up time to finish; if it stops, use exactly
    // the same Jump/backward-Duck controls available to a human player.
    auto climbName=p->AnimSequence().ToString();
    for(auto& c:climbName)c=char(std::tolower(static_cast<unsigned char>(c)));
    const bool climbing=self->health>0 && !(self->flags&2) &&
        (climbName.find("pullup")!=std::string::npos || climbName.find("hang")!=std::string::npos);
    if(climbing) {
        auto at=position(*self);
        if(!ledgeSince || length(at-ledgePosition)>12.f){ledgeSince=now;ledgePosition=at;ledgeJumpAt=now+1000;}
        if(now-ledgeSince>=1000) {
            liftActive=-1;slamStage=0;leapAt=jumpUntil=guardUntil=0;
            if(now>=ledgeJumpAt && act(ActionKind::Jump,350))ledgeJumpAt=now+800;
            if(now-ledgeSince>=2400){out.input.forward=-600;out.input.held=4;}
            return finish();
        }
    }else ledgeSince=ledgeJumpAt=0;
    if(liftActive>=0 && self->health>0){if(auto lift=liftStep(game,p,*self,liftGoal,now)){out=*lift;return finish();}}
    if(self->health<=0){act(ActionKind::Respawn,1500);return finish();}
    const auto here=position(*self);auto temperament=RuneTactics::ForDifficulty(difficulty);const auto reaction=uint64_t(temperament.reaction*1000);
    if(slamStage) {
        if(now>=slamAt) {
            if(!RuneBattleLaws::Allowed(RuneBattleLaws::Slam) || now>slamAt+300 || !RuneSlam::Heavy(p->Weapon()) || self->combat.phase || (self->flags&2)) {slamStage=0;return finish();}
            if(slamStage==1 && act(ActionKind::BloodLeap,100)){slamStage=2;slamAt=now+100;}
            else if(slamStage==2 && act(ActionKind::Attack,900)){slamStage=0;++specials;}
        }
        return finish();
    }
    auto yawDirection=[&](uint16_t yaw){float a=float(yaw)*6.2831853f/65536;return vec3(std::cos(a),std::sin(a),0);};
    auto aim=[&](vec3 at){auto d=at-here;int wanted=Rotator::FromVector(d).Yaw;int change=int16_t(uint16_t(wanted-self->angles[1]));out.input.yaw=uint16_t(self->angles[1]+std::clamp(change,-4369,4369));}; // 240 degrees/sec at 10 Hz.
    auto move=[&](vec3 at,float speed=850.f){auto d=direction(at-here);auto f=yawDirection(out.input.yaw);out.input.forward=int16_t(dot(d,f)*speed);out.input.strafe=int16_t(dot(d,vec3(-f.y,f.x,0))*speed);};
    if(leapAt) {
        const auto* leapTarget=find(world,target);
        if(RuneBattleLaws::Allowed(RuneBattleLaws::Leap) && leapTarget && leapTarget->health>0 && !(leapTarget->flags&32) && enemy(status,own,leapTarget->ref) &&
           p->FastTrace(position(*leapTarget),here) && now>=leapAt && now<=leapAt+200 && (self->combat.ready&1)) {
            out.input.yaw=leapYaw;out.input.forward=850;
            if(act(ActionKind::BloodLeap,1800)){++specials;++leaps;}
        }
        leapAt=0;return finish();
    }
    if(now<jumpUntil && !(self->flags&2)) {aim(jumpGoal);move(jumpGoal,750);return finish();}
    // If a hit or a moving platform drops us into water, use ordinary swimming
    // input to get back toward dry ground. Never teleport out of a bad route.
    auto* currentZone=game.LevelInfo->GetLocZone(here,{}).Zone;
    if(currentZone && currentZone->bWaterZone() && !currentZone->bPainZone()) {
        vec3 shore=here+yawDirection(self->angles[1])*200.f;float nearest=1e9f;
        for(auto* actor:game.Level->Actors)if(actor && actor->IsA("NavigationPoint")) {
            auto at=actor->Location();auto* zone=game.LevelInfo->GetLocZone(at,{}).Zone;
            if(zone && !zone->bWaterZone() && !zone->bPainZone() && !zone->IsA("HeadBallZone") && length(at-here)<nearest && p->FastTrace(at,here)) {nearest=length(at-here);shore=at;}
        }
        aim(shore);out.input.pitch=8192;out.input.forward=700;return finish();
    }
    bool canFight=status.mode!=GameMode::Arena || (status.arenaPhase==1 && team(status,own)<2);
    if(status.mode==GameMode::HeadBall && !status.headBallStarted)canFight=false;
    const Entity* foe=nullptr;float best=1e9f;
    if(canFight) {
        if(classic) {
            for(const auto& e:world){if(e.kind!=EntityKind::Pawn || e.health<=0 || (e.flags&32) || !enemy(status,own,e.ref))continue;
                if(rejectedAt(position(e),now) && length(position(e)-here)>160)continue;
                auto d=position(e)-here;float distance=length(d);if(distance>1600 || (distance>110 && dot(yawDirection(self->angles[1]),direction(d))<-.15f))continue;
                if(!p->FastTrace(position(e)+vec3(0,0,20),here+vec3(0,0,20)))continue;
                float score=distance*(e.ref==target?.65f:1.f);
                if(status.mode!=GameMode::Deathmatch)for(const auto& ally:world)
                    if(ally.kind==EntityKind::Pawn && ally.ref!=own && ally.health>0 && team(status,ally.ref)==team(status,own) &&
                       flat(position(ally)-position(e))<160 && p->FastTrace(position(ally),here))score+=180;
                if(score<best){foe=&e;best=score;}}
        } else {
            const auto selfFwd=yawDirection(self->angles[1]);
            std::vector<const Entity*> visiblePawns,eligibleEnemies;
            const Entity* classicFoe=nullptr;float classicBest=1e9f;
            for(const auto& e:world){
                if(e.kind!=EntityKind::Pawn || e.ref==own || e.health<=0 || (e.flags&32))continue;
                auto d=position(e)-here;float distance=length(d);
                if(distance>1600 || (distance>110 && dot(selfFwd,direction(d))<-.15f))continue;
                if(!p->FastTrace(position(e)+vec3(0,0,20),here+vec3(0,0,20)))continue;
                visiblePawns.push_back(&e);
                if(!enemy(status,own,e.ref))continue;
                if(rejectedAt(position(e),now) && distance>160)continue;
                eligibleEnemies.push_back(&e);
                float cScore=distance*(e.ref==target?.65f:1.f);
                if(cScore<classicBest){classicBest=cScore;classicFoe=&e;}
            }
            bool chosenThirdParty=false;
            for(const auto* c:eligibleEnemies){
                const auto pos=position(*c);const auto d=pos-here;const float dist=length(d);
                const auto dirToC=direction(d);const auto cFwd=yawDirection(c->angles[1]);
                const float facingUs=dot(cFwd,-dirToC);
                const bool cSwinging=swinging(replica.actor(c->ref));
                const bool cBlocking=(c->flags&4)||(c->combat.flags&2);
                float score=dist;
                if(std::abs(d.z)>64.f)score+=std::abs(d.z)*1.1f;
                // Third-party: prefer an enemy engaged with another visible pawn or swinging away from us.
                bool engagedElsewhere=false;
                for(const auto* o:visiblePawns){
                    if(o->ref==c->ref)continue;
                    auto toOther=position(*o)-pos;
                    if(flat(toOther)<250.f && std::abs(toOther.z)<96.f && dot(cFwd,direction(toOther))>.45f){engagedElsewhere=true;break;}
                }
                if(!engagedElsewhere && cSwinging && facingUs<.2f)engagedElsewhere=true;
                const bool isThirdParty=engagedElsewhere && facingUs<.35f;
                if(isThirdParty){
                    const float backFactor=std::clamp((.35f-facingUs)/1.35f,0.f,1.f);
                    score-=110.f+backFactor*160.f;
                }else if(facingUs<-.25f && dist<500.f){
                    score-=75.f;
                }
                // Wounded: prefer low-health enemies within a short walk (especially 1-hit kills).
                const float closeness=std::clamp(1.f-(dist-140.f)/600.f,.2f,1.f);
                const float missingHealth=std::clamp((100.f-float(c->health))/100.f,0.f,1.f);
                score-=missingHealth*210.f*closeness;
                if(c->health<=35)score-=120.f*closeness;
                if((c->flags&FinisherOpeningFlag) && dist<300.f)score-=85.f;
                if(cBlocking && facingUs>.5f)score+=55.f;
                // Avoid sandwiches and ignoring a closer attacker who is facing us.
                for(const auto* o:eligibleEnemies){
                    if(o->ref==c->ref)continue;
                    auto od=position(*o)-here;const float oDist=length(od);const auto dirToO=direction(od);
                    const float oFacingUs=dot(yawDirection(o->angles[1]),-dirToO);
                    const float alignment=dot(dirToC,dirToO);
                    if(oDist<420.f && alignment<-.15f){
                        const float behind=(-.15f-alignment)/.85f;
                        const float proximity=std::clamp(1.f-oDist/420.f,0.f,1.f);
                        score+=behind*proximity*(oFacingUs>.35f?300.f:140.f);
                    }
                    if(oDist+40.f<dist && oDist<300.f && oFacingUs>.45f){
                        const bool oSwing=swinging(replica.actor(o->ref));
                        const float proximity=std::clamp(1.f-oDist/300.f,.25f,1.f);
                        score+=(oSwing?220.f:130.f)*proximity;
                    }
                }
                if(status.mode!=GameMode::Deathmatch)for(const auto* o:visiblePawns)
                    if(team(status,o->ref)==team(status,own) && flat(position(*o)-pos)<160.f)score+=180.f;
                // Hysteresis: switching targets costs a reaction pause, so hold current target unless clearly beaten.
                if(c->ref==target)score-=std::max(140.f,dist*.30f);
                if(score<best){foe=c;best=score;chosenThirdParty=isThirdParty;}
            }
            if(foe && target!=foe->ref){
                if(chosenThirdParty)++thirdParty;
                if(classicFoe && foe->ref!=classicFoe->ref){
                    ++targetOverrides;
                    if(foe->health<=50)++woundedTargets;
                }
            }
        }
    }
    if(self->health<lastHealth && !foe)searchUntil=now+700;
    lastHealth=self->health;
    if(foe){searchUntil=0;if(target!=foe->ref){target=foe->ref;seenAt=now;wasThreat=false;threatAt=blockedAt=0;}remembered=position(*foe);lastSeen=now;}
    else {wasThreat=false;threatAt=blockedAt=0;if(lastSeen && now-lastSeen>2200){target={};seenAt=lastSeen=0;}}
    // Weapon-aware rules: duel-style defence (holding a guard, keeping distance)
    // only pays against a single engaged opponent; with a second enemy close by
    // it just hands that enemy free hits.
    bool crowded=false;
    if(!classic && foe)for(const auto& e:world)if(e.kind==EntityKind::Pawn && e.ref!=foe->ref && e.ref!=own && e.health>0 && !(e.flags&32) &&
        enemy(status,own,e.ref) && length(position(e)-here)<350.f){crowded=true;break;}
    if(guardUntil && !classic && foe && now<guardUntil && crowded){
        guardUntil=0;guardSawSwing=false;out.action=ActionKind::LowerShield;readyAt=std::max(readyAt,now+100);return finish();
    }
    if(guardUntil && !classic && foe && now<guardUntil){
        // Hold the guard toward the attacker. A swing that ends on the guard
        // opens a punish; kicks and bashes beat a guard, so leave their reach.
        auto* attacker=replica.actor(foe->ref);auto gap=position(*foe)-here;
        auto* attackerPawn=UObject::TryCast<UPawn>(attacker);
        const auto attackerWeapon=(attackerPawn && foe->weapon.id)?ProfileWeapon(attackerPawn,replica.actor(foe->weapon)):WeaponProfile{};
        const float reach=attackerWeapon.valid?attackerWeapon.reach+p->CollisionRadius()*.6f:120.f;
        out.input.yaw=uint16_t(Rotator::FromVector(gap).Yaw);
        if(guardBreaker(attacker) && flat(gap)<reach+20.f){
            guardUntil=0;guardSawSwing=false;out.action=ActionKind::LowerShield;readyAt=std::max(readyAt,now+150);
            auto back=here-direction(gap)*std::clamp(reach+24.f-flat(gap),48.f,110.f);
            if(safe(p,here,back)){auto f=yawDirection(out.input.yaw);auto d=direction(back-here);out.input.forward=int16_t(dot(d,f)*850);out.input.strafe=int16_t(dot(d,vec3(-f.y,f.x,0))*850);}
            ++guardEscapes;return finish();
        }
        const bool attacking=swinging(attacker);
        if(attacking)guardSawSwing=true;
        else if(guardSawSwing){
            guardUntil=0;guardSawSwing=false;out.action=ActionKind::LowerShield;readyAt=std::max(readyAt,now+100);
            foeRecoveredAt=now;++guardPunishes;return finish();
        }
        if(flat(gap)>reach+80.f){guardUntil=0;guardSawSwing=false;out.action=ActionKind::LowerShield;readyAt=std::max(readyAt,now+150);return finish();}
        out.input.held=2;return finish();
    }
    if(guardUntil){if(now<guardUntil){out.input.held=2;return finish();}out.action=ActionKind::LowerShield;guardUntil=0;guardSawSwing=false;readyAt=std::max(readyAt,now+250);return finish();}
    if(!foe && now<searchUntil){out.input.yaw=uint16_t(self->angles[1]+4369);return finish();}
    if(!foe && now<escapeUntil && flat(escapeGoal-here)>16){
        // Retain the chosen escape tangent briefly. Re-aiming at the unreachable
        // patrol goal every think otherwise walks back into the same seam.
        auto next=here+direction(escapeGoal-here)*16.f;vec3 hit,normal,floor;
        const vec3 extent(p->CollisionRadius(),p->CollisionRadius(),std::max(1.f,p->CollisionHeight()-1.f));
        auto* block=terrainTrace(p,hit,normal,next,here,extent);
        auto* support=terrainTrace(p,floor,normal,next-vec3(0,0,p->CollisionHeight()+96),next,vec3(8,8,0));
        auto* zone=support?game.LevelInfo->GetLocZone(floor+vec3(0,0,4),{}).Zone:nullptr;
        if(!block && support && normal.z>.65f && floor.z<=here.z-p->CollisionHeight()+.1f && zone && !zone->bPainZone() && !zone->IsA("HeadBallZone")){
            aim(escapeGoal);move(escapeGoal,450);return finish();
        }
    }
    escapeUntil=0;
    // Arena spectators queue normally; no attacking through the countdown or lobby.
    vec3 goal=here;bool goalSet=false;
    if(status.mode==GameMode::Arena && team(status,own)<2 && status.arenaPhase!=1)return finish();
    if(status.mode==GameMode::Arena && team(status,own)>=2){
        if(!self->weapon.id && now<equipmentUntil) {
            // Remember authored weapon-rack locations, not the hidden live
            // availability of their pickups. Visible snapshots confirm Use.
            float nearest=1e9f;
            for(auto* actor:game.Level->Actors)if(actor && actor->IsA("InventorySpot")) {
                auto* item=actor->GetUObject("markedItem");
                if(item && item->IsA("Weapon") && !item->IsA("LimbWeapon") && length(actor->Location()-here)<nearest) {
                    nearest=length(actor->Location()-here);goal=actor->Location();goalSet=true;
                }
            }
        }
        float nearest=1e9f;
        if(!goalSet)for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint")){
            auto* zone=game.LevelInfo->GetLocZone(a->Location(),{}).Zone;
            if(zone && zone->IsA("QueueZone") && length(a->Location()-here)<nearest){nearest=length(a->Location()-here);goal=a->Location();goalSet=true;}
        }
        if(!goalSet)for(auto* a:game.Level->Actors)if(a && a->IsA("QueueZone")){goal=a->Location();goalSet=true;break;}
        if(p->Region().Zone && p->Region().Zone->IsA("QueueZone"))return finish();
    }
    if(status.mode==GameMode::HeadBall){
        if(!status.headBallStarted){
            for(auto* a:game.Level->Actors)if(a && a->IsA("HBGamePump") && !a->GetBool("bAdminOverride") && a->GetByte("TeamNumber")==team(status,own)){
                if(team(status,own)>=4 || status.readyTeams[team(status,own)])return finish();
                goal=a->Location();goalSet=true;if(length(goal-here)<90){aim(goal);if(act(ActionKind::Use,1200))++objectives;return finish();}break;}
        }else if(p->Weapon() && (p->Weapon()->IsA("Head") || (limbsScore && p->Weapon()->IsA("LimbWeapon")))){
            // Goal ZoneInfo icons can be under the floor or behind the net.
            // Use the map's authored zone portal as the opening, never its icon.
            if(goalLevel!=game.Level) {
                goalLevel=game.Level;scoringOpenings.clear();
                auto* model=game.Level->Model;
                if(model)for(const auto& node:model->Nodes) {
                    if(node.NumVertices<3 || node.Surf<0 || size_t(node.Surf)>=model->Surfaces.size() ||
                       !(model->Surfaces[node.Surf].PolyFlags&PF_Portal) || node.Zone0==node.Zone1)continue;
                    vec3 center(0);for(int v=0;v<node.NumVertices;++v)center+=model->Points[model->Vertices[node.VertPool+v].Vertex];
                    center/=float(node.NumVertices);
                    auto normal=normalize(vec3(node.PlaneX,node.PlaneY,node.PlaneZ));
                    for(int zoneIndex:{node.Zone0,node.Zone1}) {
                        if(zoneIndex<0 || size_t(zoneIndex)>=model->Zones.size())continue;
                        auto* zone=model->Zones[zoneIndex].ZoneActor;
                        if(!zone || !zone->IsA("HeadBallZone"))continue;
                        for(float sign:{-1.f,1.f}) {
                            auto inside=center+normal*(sign*16.f);
                            if(game.LevelInfo->GetLocZone(inside,{}).Zone!=zone)continue;
                            auto outward=-normal*sign,approach=center+outward*72.f;
                            vec3 floor,normalHit;
                            if(terrainTrace(p,floor,normalHit,approach-vec3(0,0,512),approach+vec3(0,0,16),vec3(4,4,0)) && normalHit.z>.65f)
                                approach=floor+vec3(0,0,p->CollisionHeight()+1);
                            // Ignore internal portal faces whose "approach"
                            // is inside another scoring volume or solid scenery.
                            if(!safe(p,approach,approach) || !p->FastTrace(inside,approach))continue;
                            scoringOpenings.push_back({zone,inside,outward,approach});
                            if(debug)std::cout<<"OPEN_RUNE_AI_GOAL_PORTAL aim="<<inside.x<<","<<inside.y<<","<<inside.z<<" approach="<<approach.x<<","<<approach.y<<","<<approach.z<<std::endl;
                            break;
                        }
                    }
                }
            }
            float closest=1e9f,best=1e9f;vec3 shot;
            for(const auto& opening:scoringOpenings) {
                auto* a=opening.zone;
                if(a->GetBool("bTeamOnly") && a->GetByte("TeamNumber")!=team(status,own))continue;
                float distance=length(opening.inside-here);
                bool visible=distance<450 && p->FastTrace(opening.inside,here);
                float cost=length(opening.approach-here)+(visible?0.f:500.f);
                if(cost<best){best=cost;closest=distance;shot=opening.inside;goal=opening.approach;goalSet=true;}
            }
            if(debug && now>=debugAt && goalSet) {
                vec3 hit,normal;auto* wall=p->Trace(hit,normal,shot,here,false,{});
                std::cout<<"OPEN_RUNE_AI_GOAL_AIM visible="<<p->FastTrace(shot,here)<<" distance="<<closest<<" shot="<<shot.x<<","<<shot.y<<","<<shot.z<<" wall="<<(wall?wall->Class->Name.ToString():"None")<<" hit="<<hit.x<<","<<hit.y<<","<<hit.z<<" mass="<<p->Weapon()->Mass()<<std::endl;
            }
            if(goalSet && closest<450 && closest>80 && p->FastTrace(shot,here)) {
                aim(shot);auto delta=shot-here;float distance=flat(delta);
                // Original ThrowWeapon adds an upward component and gravity;
                // aiming the camera directly at the icon consistently misses.
                float mass=std::max(1.f,p->Weapon()->Mass());
                float forward=std::min(750.f,7500.f/mass),up=std::min(200.f,2000.f/mass);
                float speed2=forward*forward+up*up;
                float gravity=currentZone?std::max(1.f,-currentZone->ZoneGravity().z):950.f;
                float discriminant=speed2*speed2-gravity*(gravity*distance*distance+2*delta.z*speed2);
                if(debug && now>=debugAt)std::cout<<"OPEN_RUNE_AI_BALLISTIC gravity="<<gravity<<" distance="<<distance<<" height="<<delta.z<<" disc="<<discriminant<<" yaw="<<out.input.yaw<<std::endl;
                if(discriminant>=0 && distance>1) {
                    float angle=std::atan((speed2-std::sqrt(discriminant))/(gravity*distance))-std::atan2(up,forward);
                    out.input.pitch=int16_t(std::clamp(angle*65536.f/6.2831853f,-16384.f,16384.f));
                    // ThrowWeapon releases from the animated hand, not at the
                    // instant the button is pressed. Settle on the ground first
                    // so falling/momentum cannot invalidate the planned arc.
                    if((self->flags&2) && flat(p->Velocity())<25.f && dot(yawDirection(out.input.yaw),direction(delta))>.999f && act(ActionKind::Throw,1500)) {
                        ++objectives;
                        if(debug)std::cout<<"OPEN_RUNE_AI_SHOT from="<<here.x<<","<<here.y<<","<<here.z<<" goal="<<shot.x<<","<<shot.y<<","<<shot.z<<" pitch="<<out.input.pitch<<" mass="<<mass<<" gravity="<<gravity<<std::endl;
                    }
                    return finish();
                }
            }
        }
    }
    if(!goalSet && canFight && status.mode==GameMode::TeamDeathmatch && !foe && self->weapon.id && self->health>=35){
        // Rally to an observed teammate's fight, not to an unseen opponent.
        // Their visible attack animation and position are normal observations;
        // neither hidden enemy positions nor their input buttons are queried.
        float nearest=1200;
        for(const auto& ally:world)if(ally.kind==EntityKind::Pawn && ally.ref!=own && ally.health>0 && team(status,ally.ref)==team(status,own)){
            float distance=length(position(ally)-here);
            if(distance<nearest && swinging(replica.actor(ally.ref)) && p->FastTrace(position(ally)+vec3(0,0,20),here+vec3(0,0,20))){
                nearest=distance;supportGoal=position(ally);supportUntil=now+2500;
            }
        }
        if(now<supportUntil && !rejectedAt(supportGoal,now)){
            if(flat(supportGoal-here)>140){goal=supportGoal;goalSet=true;++supportMoves;}
            else {out.input.yaw=uint16_t(self->angles[1]+4369);return finish();}
        }
    }
    // Seek useful visible supplies; never know a hidden pickup's live availability.
    // Weapon-aware rules: a lone weapon has no guard, so arm up (shield or a second
    // one-handed weapon for a crossed block) while the nearest enemy is still far.
    const bool needGuard=!classic && self->weapon.id && !self->shield.id && !self->combat.offhand.id && (!foe || flat(position(*foe)-here)>450);
    if((!goalSet || (status.mode==GameMode::Arena && team(status,own)>=2 && !self->weapon.id)) && (!foe || needGuard || self->health<35 || !self->weapon.id || (status.mode==GameMode::HeadBall && status.headBallStarted))){
        float closest=700;EntityRef chosenSupply{};
        for(const auto& e:world)if(!e.owner.id && !(e.flags&32) && (e.kind==EntityKind::Weapon || e.kind==EntityKind::Limb || e.kind==EntityKind::Shield || e.kind==EntityKind::Pickup)){
            auto* a=replica.actor(e.ref);if(!a)continue;
            bool scoring=status.mode==GameMode::HeadBall && (a->IsA("Head") || (limbsScore && a->IsA("LimbWeapon")));
            if(foe && flat(position(*foe)-here)<140 && self->health>=35 && self->weapon.id && !scoring)continue;
            bool useful=((!self->weapon.id || (RuneBattleLaws::Allowed(RuneBattleLaws::Dual) && !self->shield.id && !self->combat.offhand.id && RuneDualWield::OneHanded(p->Weapon()) && RuneDualWield::OneHanded(UObject::TryCast<UWeapon>(a)))) && (e.kind==EntityKind::Weapon || e.kind==EntityKind::Limb)) || (!self->shield.id && !self->combat.offhand.id && e.kind==EntityKind::Shield) || (self->health<70 && a->IsA("Food")) ||
                (a->IsA("RuneOfPowerRefill")?self->combat.runePower<self->combat.runeCapacity:(a->IsA("RuneOfPower")&&(self->combat.runeCapacity<200||self->combat.runePower<self->combat.runeCapacity))) || scoring;
            float d=length(position(e)-here);if(rejectedAt(position(e),now))continue;
            if(useful && d<closest && std::abs(position(e).z-here.z)<120 && p->FastTrace(position(e),here)){closest=d;goal=position(e);goalSet=true;chosenSupply=e.ref;}}
        if(goalSet && closest<90){
            if(supplyTarget!=chosenSupply){supplyTarget=chosenSupply;supplyTries=0;}
            // A duplicate or unreachable pickup can remain visible after Use.
            // Give normal pickup/replication time to complete, then move on.
            if(supplyTries>=3){rejectGoal(goal,now);supplyTarget={};goalSet=false;}
            else {aim(goal);if(now>=useAt && act(ActionKind::Use,350)){useAt=now+800;++objectives;++supplyTries;}return finish();}
        }
    }
    if(!goalSet && canFight && status.mode!=GameMode::Arena && !self->weapon.id && !self->combat.offhand.id) {
        // An unarmed bot should re-equip, not remain in a shield/kick stalemate.
        // Rack locations are static map knowledge; only visible pickups above
        // confirm whether a weapon is actually there. Move on from empty racks.
        float nearest=1e9f;
        for(auto* actor:game.Level->Actors)if(actor && actor->IsA("InventorySpot")) {
            auto* item=actor->GetUObject("markedItem");
            if(!item || !item->IsA("Weapon") || item->IsA("LimbWeapon"))continue;
            float distance=length(actor->Location()-here);
            if(distance<100 || (rejectedAt(actor->Location(),now)))continue;
            if(distance<nearest){nearest=distance;goal=actor->Location();goalSet=true;}
        }
    }
    // A visible, nearby body is an opportunistic ranged attack, never an
    // objective detour or an attack through a teammate. Only authenticated
    // Use/Throw intents are emitted; the host revalidates the complete pickup.
    const Entity* heldBody=nullptr;
    for(const auto& e:world)if(((e.kind==EntityKind::Corpse&&e.attachmentJoint!=255)||(e.kind==EntityKind::Pickup&&e.physical.kind==2)) && e.owner==own){heldBody=&e;break;}
    if(heldBody) {
        if(carriedBody!=heldBody->ref){carriedBody=heldBody->ref;carrySince=now;}
        if(foe) {
            auto delta=position(*foe)-here;float distance=flat(delta);aim(position(*foe));
            bool clearLane=true;
            if(status.mode!=GameMode::Deathmatch)for(const auto& ally:world)if(ally.kind==EntityKind::Pawn && ally.health>0 && ally.ref!=own && !enemy(status,own,ally.ref)) {
                auto d=position(ally)-here;float along=dot(d,direction(delta));
                if(along>0 && along<distance && flat(d-direction(delta)*along)<60)clearLane=false;
            }
            if(clearLane && distance>140 && distance<550 && now-carrySince>650 &&
               dot(yawDirection(out.input.yaw),direction(delta))>.985f && act(ActionKind::Throw,1100)){corpseAt=now+10000;++specials;}
            else if(now-carrySince>3500)act(ActionKind::Use,500);
            return finish();
        }
        if(now-carrySince>1500)act(ActionKind::Use,500);
        return finish();
    }
    carriedBody={};carrySince=0;
    if(!goalSet && foe && (RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)||RuneBattleLaws::Allowed(RuneBattleLaws::Props)) && now>=corpseAt && now>=readyAt && RuneCorpse::Eligible(p)) {
        float distance=flat(position(*foe)-here);
        if(distance>180 && distance<500)for(const auto& e:world)if((e.kind==EntityKind::Corpse||(e.kind==EntityKind::Pickup&&e.physical.kind==2)) && !e.owner.id && !(e.flags&32) && length(position(e)-here)<100) {
            auto* body=replica.actor(e.ref);
            if(body && ((e.kind==EntityKind::Pickup&&RuneBattleLaws::Allowed(RuneBattleLaws::Props))||(e.kind==EntityKind::Corpse&&RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)&&RuneCorpse::Throwable(body))) && p->FastTrace(position(e),here) && act(ActionKind::Use,700)){corpseAt=now+3500;return finish();}
        }
    }
    if(!goalSet && foe){
        auto delta=position(*foe)-here;float distance=flat(delta);aim(position(*foe));
        bool threat=(swinging(replica.actor(foe->ref)) || foe->combat.phase==6) && distance<(foe->combat.phase==6?220:180) && dot(yawDirection(foe->angles[1]),-direction(delta))>.35f;
        if(!threat && wasThreat && !classic)foeRecoveredAt=now;
        if(threat && !wasThreat){threatAt=now;defenseRoll=random();}wasThreat=threat;
        bool blocking=(foe->flags&4)||(foe->combat.flags&2);if(blocking && !blockedAt)blockedAt=now;if(!blocking)blockedAt=0;
        if(now-seenAt<reaction)return finish();
        const bool recovering=now<readyAt;
        bool facing=dot(yawDirection(out.input.yaw),direction(delta))>.9f;
        auto* opponent=UObject::TryCast<UPawn>(replica.actor(foe->ref));
        float attackRange=p->CollisionRadius()+(opponent?opponent->CollisionRadius():18.f)+(self->combat.offhand.id?12.f:30.f);
        // Weapon-aware rules: swing when the measured blade can reach the opponent's body.
        const auto ownWeapon=classic?WeaponProfile{}:ProfileWeapon(p,p->Weapon());
        if(ownWeapon.valid && !self->combat.offhand.id)attackRange=ownWeapon.reach+(opponent?opponent->CollisionRadius():18.f)*.6f;
        // How close the opponent's own blade reaches into us (0 when unknown).
        const auto foeWeapon=(classic || !opponent || !foe->weapon.id)?WeaponProfile{}:ProfileWeapon(opponent,replica.actor(foe->weapon));
        const float danger=foeWeapon.valid?foeWeapon.reach+p->CollisionRadius()*.6f:0.f;
        const float bashReach=p->CollisionRadius()+(opponent?opponent->CollisionRadius():18.f)+32.f;
        bool inHeight=std::abs(delta.z)<std::min(42.f,p->CollisionHeight());
        // A sword swipe that meets a raised kicking leg can sever it (Kick law):
        // answer a seen kick with a strike instead of a guard.
        if(!classic && !recovering && threat && now-threatAt>=reaction && defenseRoll<temperament.blockChance && facing && inHeight &&
           self->weapon.id && p->Weapon() && RuneFinisher::Style(p->Weapon())==RuneFinisher::Family::Sword &&
           RuneBattleLaws::Allowed(RuneBattleLaws::Kick) && kicking(replica.actor(foe->ref)) && distance<attackRange+12.f){
            if(act(ActionKind::Attack,650)){++attacks;++legCounters;}return finish();
        }
        if(!recovering && threat && now-threatAt>=reaction && defenseRoll<temperament.blockChance){
            if((self->shield.id || (RuneBattleLaws::Allowed(RuneBattleLaws::Cross)&&self->combat.offhand.id)) && facing){if(act(ActionKind::RaiseShield,650)){guardUntil=now+guardHold(foeWeapon);guardSawSwing=false;++defenses;if(self->combat.offhand.id)++crossBlocks;out.input.held=2;}return finish();}
            // Without a guard, step far enough to leave the opponent's measured reach.
            const float backStep=(!classic && danger>0)?std::clamp(danger+16.f-distance,48.f,110.f):48.f;
            auto back=here-direction(delta)*backStep;if(safe(p,here,back)){move(back,700);++defenses;return finish();}
        }
        // Swing timing: compute closing-velocity pre-swing and opponent remaining wind-up
        // before speculative guard/spacing so we take clean first strikes instead of hesitating.
        bool foeWillBeatUs=false;
        if(!classic && threat && ownWeapon.valid) {
            const float foePhase=swingPhase(replica.actor(foe->ref));
            const float foeRemain=foeWeapon.valid
                ? (foePhase>=0.f ? std::max(0.f, foeWeapon.windup - foePhase*foeWeapon.duration)
                                 : std::max(0.f, foeWeapon.windup - float(now-threatAt)*.001f))
                : std::max(0.f, .20f - float(now-threatAt)*.001f);
            // Only hold off and step out when we are near the edge of their reach and can actually evade;
            // at point-blank with no block, trading beats walking backward into free hits.
            if(!crowded && danger>0.f && distance>=danger-24.f && distance<danger+36.f && foeRemain+.04f<ownWeapon.windup)
                foeWillBeatUs=true;
        }
        bool earlySwing=false;
        if(!classic && !recovering && !ownAttackBusy(p) && !foeWillBeatUs && facing && inHeight && !blocking && self->weapon.id && ownWeapon.valid && ownWeapon.windup>.05f && distance>=attackRange) {
            const vec3 selfVel(float(self->velocity[0])*.25f, float(self->velocity[1])*.25f, 0.f);
            const vec3 foeVel(float(foe->velocity[0])*.25f, float(foe->velocity[1])*.25f, 0.f);
            const vec3 relVel=foeVel-selfVel;
            const vec3 dir=direction(delta);
            const float closingSpeed=-dot(relVel, dir);
            const float leadTime=std::clamp(ownWeapon.windup, .08f, .45f);
            const float maxEarlyDist=attackRange+std::min(140.f, std::max(0.f, closingSpeed)*leadTime);
            const vec3 predDelta=vec3(delta.x, delta.y, 0.f)+relVel*leadTime;
            if(closingSpeed>35.f && distance<maxEarlyDist && length(predDelta)<attackRange*.98f)earlySwing=true;
        }
        const bool ownBusy=!classic && ownAttackBusy(p);
        const bool attackReady=!recovering && !ownBusy;
        const uint64_t attackRecovery=classic
            ? (650+uint64_t(random()*250))
            : uint64_t((std::max(ownWeapon.valid?ownWeapon.duration*.75f:temperament.recovery, temperament.recovery)+random()*.04f)*1000.f);
        const bool readyStrike=!classic && attackReady && !threat && !blocking && facing && inHeight && self->weapon.id && (distance<attackRange || earlySwing);
        if(!recovering && !threat && facing && !readyStrike &&
           (classic?(distance>80 && distance<175):(!crowded && (distance<danger+24.f || (danger<=0 && distance>80 && distance<175)))) &&
           (self->shield.id || (RuneBattleLaws::Allowed(RuneBattleLaws::Cross)&&self->combat.offhand.id)) && now>=guardAt &&
           dot(yawDirection(foe->angles[1]),-direction(delta))>.65f){
            // A short prepared guard can catch a fast wind-up that a purely
            // reactive block cannot. One roll per opportunity, with a cooldown.
            // Weapon-aware rules guard inside the opponent's measured reach when not striking first.
            guardAt=now+(classic?1800:1200);
            if(random()<temperament.blockChance*(classic?.4f:.55f) && act(ActionKind::RaiseShield,650)){
                guardUntil=now+(classic?450:guardHold(foeWeapon));guardSawSwing=false;++defenses;if(self->combat.offhand.id)++crossBlocks;out.input.held=2;return finish();
            }
        }
        if(!classic && attackReady && !threat && !blocking && facing && inHeight && foeRecoveredAt && now-foeRecoveredAt<450 && self->weapon.id) {
            // The opponent just finished a swing: step in and strike before they recover.
            if(distance<attackRange || earlySwing){
                if(act(ActionKind::Attack,attackRecovery)){
                    ++attacks;++punishes;foeRecoveredAt=0;
                    if(earlySwing){++earlySwings;swingWindupUntil=now+uint64_t(std::clamp(ownWeapon.windup,.08f,.45f)*1000.f);}
                    auto forwardStep=here+direction(delta)*24.f;
                    if(distance>attackRange*.65f && safe(p,here,forwardStep))move(position(*foe),850);
                }
                return finish();
            }
            if(distance<attackRange+56.f){move(position(*foe));return finish();}
        }
        if(!classic && !recovering && !threat && facing && self->weapon.id && p->Weapon() && now>=throwAt && (self->flags&2) &&
           distance>220 && distance<650 && std::abs(delta.z)<96 && !(blocking && dot(yawDirection(foe->angles[1]),-direction(delta))>.5f) &&
           foe->health>0 && float(foe->health)<=float(p->Weapon()->GetInt("Damage"))*1.6f && p->FastTrace(position(*foe),here)) {
            // Finishing throw at a wounded opponent who is out of melee reach.
            throwAt=now+2500;
            float mass=std::max(1.f,p->Weapon()->Mass());
            float ahead=std::min(750.f,7500.f/mass),up=std::min(200.f,2000.f/mass),speed2=ahead*ahead+up*up;
            float gravity=currentZone?std::max(1.f,-currentZone->ZoneGravity().z):950.f;
            float discriminant=speed2*speed2-gravity*(gravity*distance*distance+2*delta.z*speed2);
            if(discriminant>=0 && random()<.6f) {
                float angle=std::atan((speed2-std::sqrt(discriminant))/(gravity*distance))-std::atan2(up,ahead);
                out.input.pitch=int16_t(std::clamp(angle*65536.f/6.2831853f,-16384.f,16384.f));
                if(dot(yawDirection(out.input.yaw),direction(delta))>.995f && act(ActionKind::Throw,1500)){++specials;++throws;return finish();}
            }
        }
        if(!recovering && !threat && facing && distance<600 && now>=powerAt){
            auto* weapon=p->Weapon();int cost=weapon?weapon->GetInt("RunePowerRequired"):0;
            if(weapon && weapon->GetBool("bCanBePoweredUp") && !weapon->GetBool("bPoweredUp") && cost>0 && self->combat.runePower>=cost){
                if(act(ActionKind::Powerup,600)){powerAt=now+3000;++powers;return finish();}
            }
        }
        if(status.mode!=GameMode::Deathmatch) {
            bool occupiedLane=false;
            for(const auto& ally:world)if(ally.kind==EntityKind::Pawn && ally.ref!=own && ally.health>0 && team(status,ally.ref)==team(status,own)) {
                auto offset=position(ally)-here;float along=dot(offset,direction(delta));
                if(along>0 && along<distance && flat(offset-direction(delta)*along)<p->CollisionRadius()*2+12 && length(offset)<220 && p->FastTrace(position(ally),here))occupiedLane=true;
            }
            if(occupiedLane) {
                // Do not attack through a friend. Shared local traffic steering
                // below chooses a fixed passing/yield goal for every movement
                // path, rather than moving a flank goal with the bot each think.
                goal=position(*foe);goalSet=true;
            }
        }
        if(!goalSet){
        if(attackReady && !threat && !blocking && facing && inHeight && distance<attackRange && self->weapon.id &&
           RuneFinisher::CanFinish(p->Weapon()) && RuneBattleLaws::Allowed(RuneBattleLaws::Finishers) && now-seenAt>=reaction && ((foe->flags&FinisherOpeningFlag) || RuneFinisher::Opening(p,opponent))){
            if(act(ActionKind::Attack,attackRecovery))++attacks;
            return finish();
        }
        if(!recovering && facing && inHeight && distance<bashReach && ((blocking && blockedAt && now-blockedAt>=reaction)||(threat && now-threatAt>=reaction)) && self->shield.id && now>=bashAt && RuneBash::Compatible(p)){
            if(act(ActionKind::Bash,900)){bashAt=now+4200;++specials;++bashes;}return finish();}
        if(!recovering && facing && inHeight && distance<74 && (!blocking || !self->shield.id) && now>=kickAt && RuneKick::Compatible(p)){
            const float crowdedReach=p->CollisionRadius()+(opponent?opponent->CollisionRadius():18.f)+18.f;
            const bool useful=!self->weapon.id || (foe->combat.flags&2) || (!blocking && distance<crowdedReach);
            if(useful){
                kickAt=now+uint64_t((RuneKick::AIIntervalMin+random()*RuneKick::AIIntervalSpan)*1000);
                if(random()<RuneKick::AIChance && act(ActionKind::Kick,850)){++specials;return finish();}
            }
        }
        if(!recovering && facing && inHeight && distance>180 && distance<650 && now-seenAt>reaction+300 && !threat){
            const auto forward=direction(delta);
            bool clearLane=true;
            if(status.mode!=GameMode::Deathmatch)for(const auto& ally:world)if(ally.kind==EntityKind::Pawn && ally.ref!=own && ally.health>0 && team(status,ally.ref)==team(status,own)){
                auto offset=position(ally)-here;float along=dot(offset,forward);
                if(along>0 && along<600 && flat(offset-forward*along)<p->CollisionRadius()*2+12 && std::abs(offset.z)<80)clearLane=false;
            }
            // Rush closes a shorter gap and counters an observed held shield.
            // Leap closes a longer gap, even with a shield equipped. Check the
            // actual full travel/landing corridor instead of just its first step.
            bool rush=RuneBattleLaws::Allowed(RuneBattleLaws::Rush) && clearLane && distance<350 && (self->combat.ready&2) &&
                (!blocking || (blockedAt && now-blockedAt>=reaction)) && safe(p,here,here+forward*560.f);
            bool leap=RuneBattleLaws::Allowed(RuneBattleLaws::Leap) && clearLane && distance>=300 && (self->combat.ready&1) &&
                safe(p,here+forward*600.f,here+forward*600.f) && p->FastTrace(here+forward*600.f,here);
            if(rush && (blocking || distance<300 || !leap || random()<.65f)){
                if(act(ActionKind::ShieldCharge,2000)){out.input.held=2;++specials;++charges;}return finish();
            }
            if(leap){if(act(ActionKind::Jump,100)){leapAt=now+100;leapYaw=out.input.yaw;out.input.forward=850;}return finish();}
        }
        if(!recovering && facing && inHeight && distance>110 && distance<180 && !blocking && !threat &&
            (self->flags&2) && !(self->combat.ready&1) && now>=slamCooldown && RuneBattleLaws::Allowed(RuneBattleLaws::Slam) && RuneSlam::Heavy(p->Weapon()) && safe(p,here,here) && random()<.12f) {
            if(act(ActionKind::Jump,100)){slamStage=1;slamAt=now+100;slamCooldown=now+6000;out.input.forward=400;}return finish();
        }
        if(!classic && !readyStrike && !crowded && danger>0 && !recovering && !threat && inHeight && self->weapon.id) {
            // Out-reached by a facing, ready opponent: briefly hold just beyond their blade
            // when we don't have a clean pre-swing, waiting for a whiff.
            const bool foeFacing=dot(yawDirection(foe->angles[1]),-direction(delta))>.5f;
            const bool punishWindow=foeRecoveredAt && now-foeRecoveredAt<450;
            if(attackRange+16.f<danger && foeFacing && !punishWindow && distance<danger+12.f && distance>attackRange+8.f) {
                if(!spacingSince)spacingSince=now;
                if(now-spacingSince<700) {
                    auto tangent=direction(vec3(-delta.y,delta.x,0))*float(side);
                    auto spot=position(*foe)-direction(delta)*(danger+20.f)+tangent*24.f;
                    if(safe(p,here,spot)){move(spot,700);++spacing;return finish();}
                    side=-side;
                } else if(now-spacingSince>2400)spacingSince=0;
            } else if(distance>danger+24.f)spacingSince=0;
        }
        if(!classic && !recovering && foeWillBeatUs && facing && inHeight && !blocking && self->weapon.id) {
            const float backStep=std::clamp(danger+16.f-distance,48.f,100.f);
            auto back=here-direction(delta)*backStep;
            if(safe(p,here,back)){++swingHolds;move(back,750);return finish();}
        }
        if(attackReady && facing && inHeight && !blocking && self->weapon.id && (classic?(distance<attackRange):(!foeWillBeatUs && (distance<attackRange || earlySwing)))){
            if(act(ActionKind::Attack,attackRecovery)){
                ++attacks;
                if(earlySwing){
                    ++earlySwings;
                    swingWindupUntil=now+uint64_t(std::clamp(ownWeapon.windup,.08f,.45f)*1000.f);
                }
                if(!classic && (earlySwing || distance>attackRange*.65f)){
                    auto forwardStep=here+direction(delta)*24.f;
                    if(safe(p,here,forwardStep))move(position(*foe),850);
                }
            }
            return finish();
        }
        if(!classic && (recovering || ownBusy) && now<swingWindupUntil && inHeight && distance>attackRange*.65f && distance<attackRange+145.f){
            // Keep closing momentum through an early swing's wind-up so the blade lands on target.
            auto forwardStep=here+direction(delta)*24.f;
            if(safe(p,here,forwardStep)){move(position(*foe),850);return finish();}
        }
        if(inHeight && distance<130 && (blocking || (self->health<30 && now>=woundedAt))){
            auto tangent=vec3(-delta.y,delta.x,0);auto step=here+direction(tangent)*float(side)*48.f;
            if(safe(p,here,step)){move(step,650);if(!blocking)woundedAt=now+2200;return finish();}side=-side;
        }
        if((recovering || ownBusy) && inHeight && distance<attackRange+24){
            auto tangent=direction(vec3(-delta.y,delta.x,0))*float(side);
            auto at=here+tangent*40.f;
            if(safe(p,here,at))move(at,350);
            else if(safe(p,here,here-tangent*40.f))move(here-tangent*40.f,350);
            return finish();
        }
        goal=position(*foe);goalSet=true;
        }
    }
    if(!goalSet && target.id && lastSeen){goal=remembered;goalSet=true;}
    if(!goalSet){
        const bool arrived=flat(waypoint-here)<70 && std::abs(waypoint.z-here.z)<80;
        if(now>=patrolAt || arrived){
            if(arrived && patrolAt){
                // Remember searched areas, not unseen enemies. Otherwise the
                // nearest few nodes can keep a squad on one empty floor.
                if(searchedPatrols.size()>=8)searchedPatrols.erase(searchedPatrols.begin());
                searchedPatrols.push_back(waypoint);
            }
            vec3 center(0);int starts=0;
            for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart")){center+=a->Location();++starts;}
            center=starts?center/float(starts):here;
            std::vector<std::pair<float,vec3>> spots;
            for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint") && length(a->Location()-here)>150 && length(a->Location()-here)<5000) {
                if(rejectedAt(a->Location(),now))continue;
                auto* zone=game.LevelInfo->GetLocZone(a->Location(),{}).Zone;
                if(!zone || zone->bPainZone() || zone->bWaterZone() || zone->IsA("HeadBallZone"))continue;
                // Shared map geography, never an unseen opponent's position.
                float score=length(a->Location()-center)+length(a->Location()-here)*.25f+std::abs(a->Location().z-here.z)*.6f+random()*400.f;
                for(auto searched:searchedPatrols)if(length(a->Location()-searched)<240)score+=900;
                spots.push_back({score,a->Location()});
            }
            if(!spots.empty()) {
                std::sort(spots.begin(),spots.end(),[](const auto& a,const auto& b){return a.first<b.first;});
                size_t count=random()<.75f?std::min(size_t(4),spots.size()):spots.size();
                waypoint=spots[rng()%count].second;
            } else waypoint=here+yawDirection(uint16_t(rng()))*160.f;
            patrolAt=now+45000;route.clear();routeAt=0;}
        goal=waypoint;
    }
    if(auto lift=liftStep(game,p,*self,goal,now)){out=*lift;return finish();}
    auto next=navigate(game,p,goal,now);if(!foe)aim(next);
    auto step=here+direction(next-here)*std::clamp(flat(next-here),8.f,16.f);
    if(safe(p,here,step))move(next,std::clamp(flat(next-here)*15.f,180.f,850.f));
    else {
        auto d=direction(next-here);
        const auto landing=here+d*120.f;
        vec3 hit,normal;const vec3 rise(0,0,80),extent(p->CollisionRadius(),p->CollisionRadius(),std::max(1.f,p->CollisionHeight()-1.f));
        if((self->flags&2) && now>=jumpAt && safe(p,landing,landing) &&
           !terrainTrace(p,hit,normal,here+rise,here,extent) &&
           !terrainTrace(p,hit,normal,landing+rise,here+rise,extent)) {
            if(act(ActionKind::Jump,350)){jumpAt=now+1600;move(next);return finish();}
        }
        float bestStep=-1e9f;vec3 bestAt=here;
        for(int turn:{1,-1,2,-2,3,-3,4}) {
            float angle=float(turn*side)*.78539816f;
            auto v=vec3(d.x*std::cos(angle)-d.y*std::sin(angle),d.x*std::sin(angle)+d.y*std::cos(angle),0);
            auto candidate=here+v*16.f;
            float score=dot(v,d)-std::abs(turn)*.02f;
            if(score>bestStep && safe(p,here,candidate)){bestStep=score;bestAt=candidate;}
        }
        if(bestStep>-1e8f)move(bestAt,450);
        else if((self->flags&2) && now>=jumpAt) {
            // A correction can leave the standing capsule perched at an edge
            // beneath an overhang: rising/jumping is blocked, while a short
            // horizontal exit followed by a modest ordinary fall is safe.
            // Sweep the full capsule and require a harmless landing no
            // more than 96 units beneath the feet. Shallow water is allowed
            // only for swimmers; the existing swimming recovery finds shore.
            // Never teleport or push.
            std::vector<vec3> exits;
            for(int turn:{0,1,-1,2,-2,3,-3,4}) {
                float angle=float(turn)*.78539816f;
                exits.push_back(vec3(d.x*std::cos(angle)-d.y*std::sin(angle),d.x*std::sin(angle)+d.y*std::cos(angle),0));
            }
            // Exact axis tangents can be the only valid direction in a narrow
            // BSP seam; rotated 45-degree candidates do not necessarily find it.
            for(auto axis:{vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0)})exits.push_back(axis);
            for(auto v:exits) {
                auto exit=here+v*24.f;vec3 floor,normal;
                auto* obstruction=terrainTrace(p,hit,normal,exit,here,extent);
                auto* support=terrainTrace(p,floor,normal,exit-vec3(0,0,p->CollisionHeight()+96),exit,vec3(8,8,0));
                if(obstruction || !support || normal.z<.65f)continue;
                float drop=here.z-p->CollisionHeight()-floor.z;
                if(drop<-.1f || drop>96)continue;
                auto* zone=game.LevelInfo->GetLocZone(floor+vec3(0,0,4),{}).Zone;
                if(!zone || zone->bPainZone() || (zone->bWaterZone() && !p->IsA("RunePlayer")) || zone->IsA("HeadBallZone"))continue;
                escapeGoal=here+v*192.f;escapeUntil=now+1200;
                move(escapeGoal,450);return finish();
            }
            if(debug && now>=debugAt)std::cout<<"OPEN_RUNE_AI_BLOCKED radius="<<p->CollisionRadius()<<" height="<<p->CollisionHeight()<<" jump="<<p->JumpZ()<<" step="<<p->MaxStepHeight()<<" flags="<<int(self->flags)<<std::endl;
            if(debug && now>=debugAt) {
                for(auto d:{vec3(0,0,16),vec3(16,0,0),vec3(-16,0,0),vec3(0,16,0),vec3(0,-16,0)}) {
                    vec3 hit,normal;auto* obstruction=terrainTrace(p,hit,normal,here+d,here,extent);
                    std::cout<<"OPEN_RUNE_AI_OBSTRUCTION direction="<<d.x<<","<<d.y<<","<<d.z<<" actor="<<(obstruction?obstruction->Name.ToString():"None")<<" hit="<<hit.x<<","<<hit.y<<","<<hit.z<<" normal="<<normal.x<<","<<normal.y<<","<<normal.z<<std::endl;
                }
            }
            // Small pits/raised edges may require an ordinary jump or the
            // game's normal ledge grab. Validate an elevated landing instead
            // of insisting that it be at the takeoff floor's height.
            float gravity=currentZone?std::max(1.f,-currentZone->ZoneGravity().z):950.f;
            float riseHeight=std::clamp(p->JumpZ()*p->JumpZ()/(2*gravity),48.f,160.f);
            for(int turn:{0,1,-1,2,-2,3,-3,4}) {
                float angle=float(turn)*.78539816f;
                auto v=vec3(d.x*std::cos(angle)-d.y*std::sin(angle),d.x*std::sin(angle)+d.y*std::cos(angle),0);
                auto across=here+v*120.f;vec3 floor,normal;
                if(!terrainTrace(p,floor,normal,across-vec3(0,0,p->CollisionHeight()+64),across+vec3(0,0,riseHeight+p->CollisionHeight()),vec3(8,8,0)) || normal.z<.65f)continue;
                auto landing=floor+vec3(0,0,p->CollisionHeight()+1);
                // Compare standing centers: the grab can reach a floor one
                // body half-height above the airborne center, then the new
                // standing center is another half-height above that floor.
                if(landing.z-here.z>riseHeight+2*p->CollisionHeight() || !safe(p,landing,landing))continue;
                if(terrainTrace(p,hit,normal,here+vec3(0,0,16),here,extent))continue;
                // A grab needs contact with the ledge's face; requiring a clear
                // straight rise to the apex wrongly rejects climbable edges.
                aim(landing);
                if(dot(yawDirection(out.input.yaw),v)<.85f)return finish();
                if(act(ActionKind::Jump,350)){jumpAt=now+2200;jumpUntil=now+700;jumpGoal=landing;move(landing,750);return finish();}
            }
        }
        if(now>=spiderRetry && RuneBattleLaws::Allowed(RuneBattleLaws::Spider) && (self->flags&2)){
            vec3 n,exit;if(RuneSpider::Plan(p,goal,n,exit)){
                out.input.yaw=uint16_t(Rotator::FromVector(-n).Yaw);out.input.forward=0;
                if(act(ActionKind::Jump,100)){spiderAt=now+100;spiderRetry=now+3000;spiderNormal=n;spiderExit=exit;return finish();}
            }else spiderRetry=now+1500;
        }
        if(now>=useAt && act(ActionKind::Use,400))useAt=now+1500; // ordinary doors only
    }
    if(now>=progressAt){if(length(here-progress)<15){route.clear();routeAt=patrolAt=0;side=-side;rejectGoal(goal,now);}progress=here;progressAt=now+1500;}
    return finish();
}
}
#endif
