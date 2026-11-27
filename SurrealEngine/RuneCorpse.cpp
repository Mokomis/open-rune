#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RunePhysics.h"
#include "RuneCorpse.h"
#include "RuneRushPose.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneDualWield.h"
#include "RuneFeedback.h"
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "VM/ScriptCall.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
namespace RuneCorpse {
namespace {
bool Authority(UActor* a) {
    if(!engine || !engine->LaunchInfo.IsRune() || !a || a->NetworkReplica || a->bDeleteMe())return false;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkClient)return false;
#endif
    return true;
}
bool Alive(UPawn* p){return p && !p->bDeleteMe() && !p->bHidden() && p->Health()>0;}
USkelModel* Model(UActor* a){return UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));}
vec3 Forward(UPawn* p){float a=p->Rotation().YawRadians();return {std::cos(a),std::sin(a),0};}
bool Finished(UActor* a){return a->bAnimFinished() || (a->AnimRate()==0 && a->TweenRate()==0) || (!a->bAnimLoop() && a->AnimFrame()>=a->AnimLast()-.002f);}
int Slot(UPawn* p) {
    auto* m=Model(p);if(!m)return -1;
    for(size_t j=0;j<m->Joints.size() && j<50;++j)if(m->Joints[j].Name=="torso")return int(j);
    return -1;
}
void Position(UActor* a,vec3 at) {
    auto& c=a->XLevel()->Collision;c.RemoveFromCollision(a);a->Location()=at;a->UpdateActorZone();c.AddToCollision(a);
}
vec3 Offset(UActor* a) {
    auto* m=Model(a);if(!m)return vec3(0);
    return (Coords::Rotation(a->Rotation()).ToMatrix()*mat4::scale(a->DrawScale())*
        Coords::Rotation(m->RotationOffset).ToMatrix()*vec4(a->CorpseBody.center,0)).xyz();
}
void Center(UActor* a) {
    auto& s=a->CorpseBody;if(s.centered)return;
    auto* m=Model(a);if(!m)return;
    m->PrepareMesh(a->GetByte("SkelMesh"),a->AnimSequence(),a->AnimFrame(),a);
    vec3 low(1e20f),high(-1e20f);auto* groups=static_cast<int*>(a->GetProperty("SkelGroupFlags"));bool any=false;
    for(auto& t:m->Tris)if(!(groups[t.TextureIndex]&PF_Invisible))for(auto index:t.Indices)if(size_t(index)<m->Verts.size()) {
        auto v=m->Verts[index];for(int axis=0;axis<3;++axis){low[axis]=std::min(low[axis],v[axis]);high[axis]=std::max(high[axis],v[axis]);}any=true;
    }
    s.center=any?(low+high)*.5f:vec3(0);s.lower=low;s.upper=high;s.supportDepth=0;
    if(Posed(a)) {
        auto bones=m->ActorJointMatrices(a);int pelvis=-1,neck=-1;
        for(size_t j=0;j<m->Joints.size();++j){if(m->Joints[j].Name=="pelvis")pelvis=int(j);if(m->Joints[j].Name=="neck")neck=int(j);}
        if(pelvis>=0 && neck>=0)s.center=((bones[pelvis]*vec4(0,0,0,1)).xyz()+(bones[neck]*vec4(0,0,0,1)).xyz())*.5f;
        auto toActor=mat4::scale(a->DrawScale())*Coords::Rotation(m->RotationOffset).ToMatrix();
        auto core=[&](int joint){auto name=m->Joints[joint].Name.ToString();for(char& c:name)c=char(std::tolower((unsigned char)c));return name=="pelvis" || name=="torso" || name=="chest" || name.starts_with("spine");};
        auto* geometry=GetRuneGeometry(a);int variant=a->GetByte("SkelMesh");
        if(geometry && !geometry->Meshes.empty()) {
            if(variant>=int(geometry->Meshes.size()))variant=0;
            const auto& vertices=geometry->Meshes[variant].Vertices;
            for(size_t i=0;i<vertices.size() && i<m->Verts.size();++i)if(core(vertices[i].Joint[0]) || (vertices[i].Weight<1 && core(vertices[i].Joint[1])))
                s.supportDepth=std::max(s.supportDepth,-(toActor*vec4(m->Verts[i]-s.center,0)).z);
        }
        s.supportDepth=std::clamp(s.supportDepth,3.f*a->DrawScale(),24.f*a->DrawScale());
    }
    s.centered=true;
}
vec3 CarryPoint(UPawn* p,UActor* body) {
    auto* m=Model(p);auto bones=m->ActorJointMatrices(p);auto world=GetRuneMeshToWorld(p);
    vec3 hands(0);int count=0;
    for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name=="lwrist" || m->Joints[j].Name=="rwrist") {
        hands+=(world*bones[j]*vec4(0,0,0,1)).xyz();++count;
    }
    if(count)hands/=float(count);else hands=p->Location()+vec3(0,0,p->CollisionHeight());
    auto* bm=Model(body);auto r=Coords::Rotation(Rotator(0,p->Rotation().Yaw+16384,0)).ToMatrix()*mat4::scale(body->DrawScale())*Coords::Rotation(bm->RotationOffset).ToMatrix();
    auto half=(body->CorpseBody.upper-body->CorpseBody.lower)*.5f;
    float depth=body->CorpseBody.supportDepth>0?body->CorpseBody.supportDepth:
        std::abs(r[2])*half.x+std::abs(r[6])*half.y+std::abs(r[10])*half.z;
    // Rest on the upper palm surface instead of burying the hands inside the torso.
    return hands+vec3(0,0,depth+1.f);
}
bool Clear(UPawn* p,vec3 point,float radius=18.f) {
    TraceFlags flags;flags.world=true;flags.movers=true;
    auto h=p->XLevel()->Collision.TraceFirstHit(p->Location(),point,p,vec3(radius),flags);
    return h.Fraction>=1;
}
void Unlink(UPawn* p,UActor* body) {
    if(body->GetUObject("AttachParent")==p) {
        auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));int j=body->GetByte("AttachParentJoint");
        if(j<50 && children[j]==body)children[j]=nullptr;
    }
    body->bCarriedItem()=false;body->SetObject("AttachParent",nullptr);body->SetByte("AttachParentJoint",0);
    body->SetOwner(nullptr);body->SetBase(nullptr,true);
}
void Restore(UActor* b) {
    auto& s=b->CorpseBody;if(!s.active)return;
    // Return to the authored ground pose around the same physical centre.
    s.active=s.flying=false;s.centered=false;Center(b);
    auto* m=Model(b);auto at=b->Location()-Offset(b)-b->PrePivot()-(m?m->PositionOffset:vec3(0));
    b->SetCollision(s.collide,s.blockActors,s.blockPlayers);b->bCollideWorld()=s.world;
    b->SetCollisionSize(s.radius,s.height);b->LifeSpan()=s.life;b->Velocity()=vec3(0);
    s.active=s.flying=false;s.thrower=nullptr;s.victims.clear();Position(b,at);
    b->SetPhysics(PHYS_Falling);
}
void Release(UPawn* p,bool launch) {
    auto& c=p->CorpseCarry;auto* b=c.body;if(!b)return;
    c.body=nullptr;c.release=-1;c.cooldown=p->IsA("RunePlayer")?1.2f:8.f;p->bFire()=p->bAltFire()=0;
    if(b->bDeleteMe())return;
    auto& s=b->CorpseBody;Unlink(p,b);s.flying=true;s.age=s.carry=0;s.thrower=launch?p:nullptr;s.victims.clear();
    vec3 f=Forward(p);b->Velocity()=launch?f*720.f+vec3(0,0,125):f*35.f;
    b->Rotation().Pitch=b->Rotation().Roll=0;b->SetPhysics(PHYS_None);
    if(RunePhysics::mode()) {auto velocity=b->Velocity();b->SetCollision(s.collide,s.blockActors,s.blockPlayers);b->bCollideWorld()=s.world;b->SetCollisionSize(s.radius,s.height);b->LifeSpan()=s.life;if(RunePhysics::Ragdoll(b,velocity,launch?p:nullptr)){s.active=s.flying=false;s.thrower=nullptr;s.centered=false;}}
    if(launch){++c.throws;if(p->HasProperty("WeaponThrowSound"))CallEvent(p,"PlaySound",{ExpressionValue::ObjectValue(p->GetUObject("WeaponThrowSound"))});}
    LogMessage(std::string("RUNE_CORPSE_")+(launch?"THROW":"DROP")+" actor="+p->Name.ToString());
}
bool Friends(UPawn* a,UPawn* b) {
    if(!a || !b || a==b)return true;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkAuthority && engine->GameInfo && ((engine->GameInfo->IsA("TeamGame") && engine->GameInfo->GetFloat("FriendlyFireScale")==0) || engine->GameInfo->IsA("ArenaGameInfo"))) {
        auto* x=a->PlayerReplicationInfo();auto* y=b->PlayerReplicationInfo();
        return x && y && x->GetByte("Team")==y->GetByte("Team");
    }
#endif
    return !a->IsA("RunePlayer") && !b->IsA("RunePlayer") && a->Enemy()!=b;
}
void Impact(UActor* b,UPawn* target) {
    auto& s=b->CorpseBody;auto* p=s.thrower;
    if(!Alive(p) || !Alive(target) || Friends(p,target))return;
    if(engine->GameInfo && engine->GameInfo->IsA("HeadBallGame") && !engine->GameInfo->GetBool("bGameStarted"))return;
    // A frozen impact direction is used for guard evaluation, even if the
    // thrower has moved since release. Do not damage repeatedly while touching.
    auto d=b->Velocity();d.z=0;if(length(d)<150)return;d=normalize(d);
    auto toward=-d;float yaw=target->Rotation().YawRadians();bool facing=dot(vec3(std::cos(yaw),std::sin(yaw),0),toward)>.25f;
    auto* shield=UObject::TryCast<UActor>(target->GetUObject("Shield"));
    bool blocked=facing && shield && !shield->bDeleteMe() && shield->GetInt("Health")>0 && shield->GetStateName()=="Active" && shield->GetUObject("AttachParent")==target;
    bool crossed=facing && RuneDualWield::Equipped(target) && target->Dual.guard && target->Dual.guardBlend>=.6f;
    // Host/authority alone applies damage; ordinary damage callbacks retain
    // armour, rune effects, pain, deaths and match scoring.
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->prepareCombat(p,target);
#endif
    if(blocked){++p->CorpseCarry.blocks;CallEvent(shield,"PlayHitSound",{ExpressionValue::NameValue("blunt")});}
    else {
        CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(crossed?7:18),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
        if(crossed)RuneDualWield::BreakGuard(target,.25f);
    }
    float force=blocked?55.f:crossed?130.f:320.f;
    if(Alive(target))CallEvent(target,"AddVelocity",{ExpressionValue::VectorValue(d*force*std::clamp(100.f/std::max(1.f,target->Mass()),.25f,1.2f)+vec3(0,0,blocked?0:45))});
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->recordWeapon(p,target,blocked);
#endif
    RuneFeedback::Impact(p,target,blocked,false);++p->CorpseCarry.hits;
    LogMessage("RUNE_CORPSE_HIT target="+target->Name.ToString()+" blocked="+std::to_string(blocked));
}
}
vec3 InteractionPoint(UActor* body) {
    // Articulated corpses keep the authored actor pivot, which can finish
    // below the floor. Test reach/occlusion against the visible torso instead.
    auto* m=Model(body);if(!m)return body->Location();
    auto bones=m->ActorJointMatrices(body);auto world=GetRuneMeshToWorld(body);
    vec3 center(0);int count=0;
    for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name=="pelvis" || m->Joints[j].Name=="neck") {
        center+=(world*bones[j]*vec4(0,0,0,1)).xyz();++count;
    }
    return count?center/float(count)+vec3(0,0,3):body->Location();
}
bool Throwable(UActor* a) {
    if(!a || a->bDeleteMe() || a->bHidden() || !a->IsA("RuneCarcass") || RunePhysics::Impaled(a) || (!a->NetworkReplica && a->GetBool("bDecorative")) || !Model(a))return false;
    auto* m=Model(a);unsigned joints=0;
    // Goblins use Weapon/Shield joints; human weapon-joint names are not a
    // reliable body-type test. Require a head, torso and both articulated arms.
    for(auto& j:m->Joints){if(j.Name=="head")joints|=1;if(j.Name=="torso")joints|=2;if(j.Name=="lwrist")joints|=4;if(j.Name=="rwrist")joints|=8;}
    bool humanoid=joints==15;
    if(!humanoid || a->DrawScale()<=0 || a->DrawScale()>1.5f || a->CollisionRadius()>45 || a->CollisionHeight()>65 || a->Mass()>250)return false;
    // Cap group numbers vary across the original roster. Severing replaces a
    // visible group's skin with gore; inspect those materials, not fixed IDs.
    auto* groups=static_cast<int*>(a->GetProperty("SkelGroupFlags"));
    auto** skins=static_cast<UObject**>(a->GetProperty("SkelGroupSkins"));
    for(int i=0;i<16;++i)if(!(groups[i]&PF_Invisible) && skins[i]) {
        auto name=skins[i]->Name.ToString();for(char& c:name)c=char(std::tolower((unsigned char)c));
        if(name.find("gore")!=std::string::npos)return false;
    }
    return Finished(a);
}
bool Eligible(UPawn* p) {
    if(!Alive(p) || (!p->IsA("RunePlayer") && !p->IsA("Viking")) || !RuneRushPose::Supported(p))return false;
    auto* parts=static_cast<int*>(p->GetProperty("BodyPartHealth"));if(parts[1]<=0 || parts[3]<=0)return false;
    if(auto* pp=UObject::TryCast<UPlayerPawn>(p))return !pp->ViewTarget() && (p->NetworkReplica || p->GetStateName()=="PlayerWalking");
    return p->IsA("Viking") && !(p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")) && p->GetStateName()=="Fighting";
}
UActor* Held(UPawn* p) {
    if(!p)return nullptr;
    auto* b=p->CorpseCarry.body;
    if(b && !b->bDeleteMe() && b->bCarriedItem() && b->GetUObject("AttachParent")==p)return b;
    // Replica JointChild arrays deliberately are not installed by snapshots.
    if(p->NetworkReplica)for(auto* child:p->ChildActors)if(child && !child->bDeleteMe() && child->IsA("Carcass") && child->bCarriedItem() && child->GetUObject("AttachParent")==p)return child;
    return nullptr;
}
bool Busy(UPawn* p){return Held(p)!=nullptr||RunePhysics::Carrying(p);}
float SpeedScale(UPawn* p){return Busy(p)?.62f:1.f;}
float Windup(UPawn* p){return p && p->AnimSequence()=="ATK_ALL_throw1_AA0S" && p->AnimFrame()>=0 && p->AnimFrame()<.9f?std::clamp(p->AnimFrame()*2.f,0.f,1.f):0.f;}

bool Pickup(UPawn* p,UActor* b) {
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Corpse) || !Authority(p) || !Authority(b) || !Eligible(p) || !Throwable(b) || Busy(p) || p->CorpseCarry.cooldown>0 ||
       p->Physics()!=PHYS_Walking || p->LadderMode || (p->Weapon() && p->Weapon()->IsA("NonStow")) || b->bCarriedItem() || b->CorpseBody.active || (!RunePhysics::Controls(b)&&b->Physics()!=PHYS_None) ||
       p->RuneKickActive || p->RuneBashPhase || RuneBloodlust::Busy(p) || p->Tactics.attacking ||
       p->GetBool("bSwingingHigh") || p->GetBool("bSwingingLow"))return false;
    auto* proxy=p->GetUObject("AnimProxy");if(proxy && proxy->GetStateName()!="Idle")return false;
    auto reach=InteractionPoint(b);
    if(length(reach-p->Location())>115 || !p->FastTrace(reach,p->Location()))return false;
    int j=Slot(p);if(j<0 || static_cast<UObject**>(p->GetProperty("JointChild"))[j])return false;
    vec3 anchor=p->Location()+vec3(0,0,p->CollisionHeight()+28)+Forward(p)*10.f;
    if(!Clear(p,anchor,24))return false;
    if(RunePhysics::Controls(b)){RunePhysics::Release(b);b->SetPhysics(PHYS_None);}
    RuneDualWield::Unequip(p);CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    for(auto* w:{p->Weapon(),p->Dual.offhand})if(w){CallEvent(w,"FinishAttack");CallEvent(w,"DisableSwipeTrail");}
    auto& s=b->CorpseBody;s.active=true;s.flying=false;s.centered=false;s.age=0;s.thrower=nullptr;ResetMotion(b);Center(b);
    s.collide=b->bCollideActors();s.blockActors=b->bBlockActors();s.blockPlayers=b->bBlockPlayers();s.world=b->bCollideWorld();
    s.radius=b->CollisionRadius();s.height=b->CollisionHeight();s.life=b->LifeSpan();
    b->SetBase(nullptr,true);b->SetPhysics(PHYS_None);b->SetCollision(false,false,false);b->bCollideWorld()=false;
    b->LifeSpan()=0;b->Velocity()=vec3(0);b->AnimRate()=b->TweenRate()=0;b->bAnimFinished()=true;
    b->SetOwner(p);CallEvent(p,"AttachActorToJoint",{ExpressionValue::ObjectValue(b),ExpressionValue::IntValue(j)});
    b->Rotation()=Rotator(0,p->Rotation().Yaw+16384,0);
    auto& c=p->CorpseCarry;c.body=b;c.age=0;c.release=-1;++c.pickups;
    p->bFire()=p->bAltFire()=0;p->Dual.guard=false;p->Dual.guardBlend=0;
    p->Tactics.attacking=p->Tactics.blocking=false;p->Tactics.recovery=.5f;
    Position(b,CarryPoint(p,b));
    LogMessage("RUNE_CORPSE_PICKUP actor="+p->Name.ToString()+" body="+b->Name.ToString());return true;
}
bool Use(UPawn* p) {
    if(!Authority(p))return false;
    if(Busy(p)){Drop(p);return true;}
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)||!Eligible(p))return false;
    // Keep the original weapon/food/door/quest interaction priority.
    for(auto* a:p->XLevel()->Actors)if(a && a!=p && !a->bDeleteMe() && !a->NetworkReplica && !RunePhysics::IsProp(a) && (!a->bHidden()||!a->IsA("Inventory")) && !a->IsA("Carcass") && length(a->Location()-p->Location())<=100)
        if(CallEvent(a,"CanBeUsed",{ExpressionValue::ObjectValue(p)}).ToBool())return false;
    UActor* best=nullptr;float nearest=115;
    for(auto* a:p->XLevel()->Actors)if(Throwable(a)) {auto reach=InteractionPoint(a);float d=length(reach-p->Location());if(d<nearest && !a->bCarriedItem() && !a->CorpseBody.active && p->FastTrace(reach,p->Location())){best=a;nearest=d;}}
    return best && Pickup(p,best);
}
bool Throw(UPawn* p) {
    if(!Authority(p) || !Busy(p))return false;
    if(RunePhysics::Carrying(p))return RunePhysics::ThrowProp(p);
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)){Drop(p);return true;}
    auto& c=p->CorpseCarry;if(c.release<0 && c.age>=.35f) {
        c.release=.32f;
        for(auto& sequence:Model(p)->Sequences)if(sequence.Info.Name=="ATK_ALL_throw1_AA0S"){p->PlayAnim(sequence.Info.Name,1,.06f);break;}
    }
    return true;
}
void Drop(UPawn* p){if(p && Authority(p)){if(RunePhysics::Carrying(p))RunePhysics::UseProp(p);else Release(p,false);}}
void Tick(UPawn* p,float elapsed) {
    if(!Authority(p))return;
    auto& c=p->CorpseCarry;c.cooldown=std::max(0.f,c.cooldown-elapsed);
    if(RunePhysics::Carrying(p)){
        c.age+=elapsed;if(!RuneBattleLaws::Allowed(RuneBattleLaws::Props)||!Alive(p)||(!p->IsA("RunePlayer")&&p->GetStateName()!="Fighting")){Drop(p);return;}
        if(!p->IsA("RunePlayer")){auto* enemy=p->Enemy();if(c.age>1&&Alive(enemy)&&p->CanSee(enemy)&&length(enemy->Location()-p->Location())>140){auto d=enemy->Location()-p->Location();p->SetRotation(Rotator::FromVector(vec3(d.xy(),0)));RunePhysics::ThrowProp(p);}else if(c.age>3)Drop(p);}return;
    }
    auto* b=Held(p);
    if(b&&!RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)){Drop(p);return;}
    if(!b){
        c.body=nullptr;c.think-=elapsed;
        if(!p->IsA("RunePlayer") && c.cooldown<=0 && c.think<=0) {
            c.think=.8f;auto* enemy=p->Enemy();
            if(Eligible(p) && Alive(enemy) && p->CanSee(enemy)) {
                float range=length(enemy->Location()-p->Location());
                if(range>180 && range<500){for(auto* a:p->XLevel()->Actors)
                    if(RuneBattleLaws::Allowed(RuneBattleLaws::Corpse) && Throwable(a) && length(a->Location()-p->Location())<100 && Pickup(p,a))break;
                    if(RuneBattleLaws::Allowed(RuneBattleLaws::Props)&&!Busy(p)&&RunePhysics::mode())RunePhysics::UseProp(p);
                }
            }
        }
        return;
    }
    c.age+=elapsed;
    if(!Eligible(p) || p->Physics()!=PHYS_Walking || p->LadderMode || p->RuneKickActive || p->RuneBashPhase || RuneBloodlust::Busy(p) || !Throwable(b)) {Drop(p);return;}
    vec3 at=CarryPoint(p,b);
    if(!Clear(p,at,24)){Drop(p);return;}
    Position(b,at);b->Rotation()=Rotator(0,p->Rotation().Yaw+16384,0);
    p->bFire()=p->bAltFire()=0;p->Dual.guard=false;p->Dual.guardBlend=0;
    if(c.release>=0){c.release-=elapsed;if(c.release<=0)Release(p,true);}
    if(!p->IsA("RunePlayer") && c.age>1.f) {
        auto* enemy=p->Enemy();if(Alive(enemy) && length(enemy->Location()-p->Location())>140 && p->CanSee(enemy)) {
            auto d=enemy->Location()-p->Location();p->SetRotation(Rotator::FromVector(vec3(d.xy(),0)));Throw(p);
        }else if(c.age>3)Drop(p);
    }
}
bool TickBody(UActor* b,float elapsed) {
    auto& s=b->CorpseBody;if(!s.active || !Authority(b))return false;
    if(!s.flying){UpdateRuneAttachments(b);return true;}
    s.carry=std::min(.15f,s.carry+std::max(0.f,elapsed));
    std::vector<UPawn*> targets;
    for(auto* p=b->Level()->PawnList();p;p=p->nextPawn())if(Alive(p) && !p->NetworkReplica)targets.push_back(p);
    for(int n=0;s.carry>=1.f/120 && n<18;++n) {
        constexpr float dt=1.f/120;s.carry-=dt;s.age+=dt;
        auto* zone=b->Region().Zone;
        if(!zone || zone->bWaterZone() || zone->bPainZone() || s.age>4.f){Restore(b);break;}
        b->Velocity()+=zone->ZoneGravity()*dt;
        vec3 from=b->Location(),to=from+b->Velocity()*dt;
        TraceFlags flags;flags.world=true;flags.movers=true;
        auto hit=b->XLevel()->Collision.TraceFirstHit(from,to,b,vec3(18,18,14),flags);
        vec3 end=from+(to-from)*hit.Fraction;
        // Swept pawn tests are separate from world contacts, so friendly/dead
        // bodies cannot mask an opponent or make a projectile tunnel a wall.
        for(auto* t:targets)if(Alive(t) && t!=s.thrower &&
            std::find(s.victims.begin(),s.victims.end(),t)==s.victims.end()) {
            vec3 line=end-from;float q=dot(line,line);float u=q>.00001f?std::clamp(dot(t->Location()-from,line)/q,0.f,1.f):0;
            auto contactPoint=from+line*u,delta=contactPoint-t->Location();
            if(std::abs(delta.z)<=t->CollisionHeight()+14 && length(delta.xy())<=t->CollisionRadius()+18 && b->FastTrace(t->Location(),contactPoint)) {
                s.victims.push_back(t);Impact(b,t);
                if(!Friends(s.thrower,t))b->Velocity()*=.65f;
            }
        }
        Position(b,end);
        if(hit.Fraction<1) {
            if(hit.Normal.z>.6f){Position(b,end+hit.Normal*.5f);Restore(b);break;}
            b->Velocity()=(b->Velocity()-hit.Normal*dot(b->Velocity(),hit.Normal))* .4f;
            Position(b,end+hit.Normal*.5f);
        }
    }
    UpdateRuneAttachments(b);
    return true;
}
bool Posed(UActor* b){return b && b->IsA("RuneCarcass") && (b->CorpseBody.active || b->CorpseBody.presented || (b->bCarriedItem() && b->GetUObject("AttachParent")));}
void Presentation(UActor* b,bool posed) {
    if(b->CorpseBody.presented!=posed){b->CorpseBody.presented=posed;b->CorpseBody.centered=false;ResetMotion(b);}
}
bool CustomTransform(UActor* b){return Posed(b);}
mat4 MeshToWorld(UActor* b) {
    Center(b);auto* m=Model(b);auto* p=UObject::TryCast<UPawn>(b->GetUObject("AttachParent"));vec3 at=b->Location();Rotator r=b->Rotation();
    if(p && b->bCarriedItem()){at=CarryPoint(p,b);r={0,p->Rotation().Yaw+16384,0};}
    return mat4::translate(at)*Coords::Rotation(r).ToMatrix()*mat4::scale(b->DrawScale())*
        Coords::Rotation(m->RotationOffset).ToMatrix()*mat4::translate(-b->CorpseBody.center);
}
vec3 SnapshotLocation(UActor* b,vec3 ordinary) {
    if(!b->CorpseBody.active || b->bCarriedItem())return ordinary;
    return b->Location(); // Active corpses use the explicit centred presentation bit.

}
void OnDestroy(UActor* a) {
    if(auto* p=UObject::TryCast<UPawn>(a))Drop(p);
    if(a->CorpseBody.active)if(auto* p=UObject::TryCast<UPawn>(a->GetUObject("AttachParent"))){Unlink(p,a);if(p->CorpseCarry.body==a)p->CorpseCarry.body=nullptr;}
}
void BeforeSave() {
    if(!engine || !engine->LaunchInfo.IsRune() || !engine->Level)return;
    for(auto* a:engine->Level->Actors)if(a && !a->NetworkReplica && !a->bDeleteMe())if(auto* p=UObject::TryCast<UPawn>(a))Drop(p);
    for(auto* a:engine->Level->Actors)if(a && !a->bDeleteMe() && a->CorpseBody.active)Restore(a);
}
bool ScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>&,ExpressionValue& result) {
    if(!engine || !engine->LaunchInfo.IsRune())return false;
    auto* p=UObject::TryCast<UPawn>(object);
    if(!p)if(auto* a=UObject::TryCast<UActor>(object);a && a->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(a->Owner());
    if(!p || !Authority(p))return false;
    auto n=f->Name;
    if(n=="Died" || n=="Destroyed" || n=="LimbSevered"){Drop(p);return false;}
    if(n=="Use" && object==p)return Use(p);
    if(!Busy(p))return false;
    if((n=="Fire" || n=="Throw") && object==p)return Throw(p);
    if(n=="Jump" || n=="AltFire" || n=="SwitchWeapon" || n=="Powerup" || n=="GrabEdge" || n=="DropWeapon" || n=="ThrowWeapon" || n=="UseNotify" || n=="WeaponActivate" || n=="SwipeEffectStart" || n=="ActivateShield" || n=="Attack" || n=="Defend") {
        result=ExpressionValue::BoolValue(false);return true;
    }
    return false;
}
}
