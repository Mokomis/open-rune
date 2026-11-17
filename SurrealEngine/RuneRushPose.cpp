#include "Precomp.h"
#include "RunePhysics.h"
#include "Animation/RuneIKMath.h"
#include "RuneRushPose.h"
#include "RuneBloodlust.h"
#include "RuneCorpse.h"
#include "Engine.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"

namespace RuneRushPose {
namespace {
mat4 InverseAffine(const mat4& m){
    auto inv=mat3::inverse(mat3(m));auto out=mat4::identity();
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)out[c*4+r]=inv[c*3+r];
    auto t=inv*vec3(-m[12],-m[13],-m[14]);out[12]=t.x;out[13]=t.y;out[14]=t.z;return out;
}
int Joint(const USkelModel& m,const char* name){for(size_t i=0;i<m.Joints.size();++i)if(m.Joints[i].Name==name)return (int)i;return -1;}
int Sequence(const USkelModel& m,const NameString& name){for(size_t i=0;i<m.Sequences.size();++i)if(m.Sequences[i].Info.Name==name)return (int)i;return -1;}
float Smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
vec3 JointPoint(const mat4& m){return {m[12],m[13],m[14]};}
mat4 At(mat4 m,vec3 p){m[12]=p.x;m[13]=p.y;m[14]=p.z;return m;}
mat4 Turn(vec3 a,vec3 b){
    if(length(a)<.001f||length(b)<.001f)return mat4::identity();
    a=normalize(a);b=normalize(b);float d=std::clamp(dot(a,b),-1.f,1.f);
    if(d>.99999f)return mat4::identity();
    if(d<-.99999f){auto axis=cross(a,vec3(1,0,0));if(length(axis)<.01f)axis=cross(a,vec3(0,1,0));axis=normalize(axis);return mat4::quaternion(axis.x,axis.y,axis.z,0);}
    auto axis=cross(a,b);float k=1.f/std::sqrt(2*(1+d));return mat4::quaternion(axis.x*k,axis.y*k,axis.z*k,(1+d)*k);
}
RuneJointPose Local(const mat4& m){
    RuneJointPose p;p.Position=JointPoint(m);mat4 rotation=m;
    for(int c=0;c<3;++c){p.Scale[c]=length(vec3(m[c*4],m[c*4+1],m[c*4+2]));if(p.Scale[c]>.001f)for(int r=0;r<3;++r)rotation[c*4+r]/=p.Scale[c];}
    p.Rotation=RunePoseEuler(rotation);return p;
}
void Rotate(const USkelModel& m,Array<RuneJointPose>& pose,int j,const mat4& worldRotation,const Array<mat4>& bones){
    auto desired=At(worldRotation,JointPoint(bones[j]));
    auto local=Local(InverseAffine(bones[m.Joints[j].Parent])*desired);
    pose[j].Rotation=local.Rotation; // retain actual bone lengths and scales
}
void Arm(const USkelModel& m,Array<RuneJointPose>& pose,bool left,const mat4& wristTarget,vec3 pole){
    int shoulder=Joint(m,left?"lshouldb":"rshouldb"),elbow=Joint(m,left?"lelbow":"relbow"),wrist=Joint(m,left?"lwrist":"rwrist");
    if(shoulder<1 || elbow<1 || wrist<1)return;
    auto bones=m.JointMatrices(pose);auto start=JointPoint(bones[shoulder]),target=JointPoint(wristTarget);
    auto solution=RuneIK::TwoBone(start,JointPoint(bones[elbow]),JointPoint(bones[wrist]),target,pole);
    if(!solution.valid)return;
    target=solution.end;auto bend=solution.joint;
    Rotate(m,pose,shoulder,Turn(JointPoint(bones[elbow])-start,bend-start)*bones[shoulder],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,elbow,Turn(JointPoint(bones[wrist])-JointPoint(bones[elbow]),target-JointPoint(bones[elbow]))*bones[elbow],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,wrist,wristTarget,bones);
}
Array<RuneJointPose> Sample(const USkelModel& m,int sequence,float t,bool loop=false){
    int n=m.Sequences[sequence].Info.NumFrames;
    return m.SampleFrame(sequence,loop?t:std::clamp(t,0.f,1.f)*(n-1.f)/n,loop);
}
}
bool Supported(UPawn* p){
    auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));if(!m)return false;
    for(auto* name:{"chest","neck","head","lshouldb","lelbow","lwrist","rshouldb","relbow","rwrist","attach_hand","attach_hammer"})if(Joint(*m,name)<0)return false;
    return Sequence(*m,"neutral_idle")>=0 && Sequence(*m,"H3_DefendAttack")>=0;
}
void Apply(const USkelModel& m,UActor* actor,Array<RuneJointPose>& pose){
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p || !p->Bloodlust.rushing || !RuneBloodlust::Busy(p) || p->Health()<=0 || !Supported(p))return;
    auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    if(!shield || shield->bDeleteMe() || shield->bHidden() || shield->GetUObject("AttachParent")!=p)return;
    int shieldJoint=shield->GetByte("AttachParentJoint");if(shieldJoint>=(int)m.Joints.size())return;
    const auto& s=p->Bloodlust;
    auto* weapon=p->Weapon();int type=weapon?weapon->GetByte("MeleeType"):2;
    int stow=Sequence(m,type==0?"IDL_ALL_sstow1_AA0S":type==1?"IDL_ALL_hstow1_AA0S":"IDL_ALL_xstow1_AA0S");
    int idle=Sequence(m,"neutral_idle"),guard=Sequence(m,"H3_DefendAttack");
    float prepare=p->IsA("RunePlayer")?PlayerPrepare:EnemyPrepare;
    bool windup=s.phase==RuneBloodlust::ChargeWindup,recovery=s.phase==RuneBloodlust::Recovery;
    float brace=windup?Smooth((s.age-StowSeconds)/(prepare-StowSeconds)):
        recovery?1-Smooth(s.age/RecoverSeconds):1;
    // Authored stow / reverse-draw, followed by a procedural brace. Leg motion
    // comes from the original run cycle only after the preparation completes.
    if(windup || recovery){
        auto stowing=stow>=0?Sample(m,stow,windup?s.age/StowSeconds:1-s.age/RecoverSeconds):Sample(m,idle,0);
        float blend=windup?Smooth(s.age/.07f):1.f;
        for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(pose[j],stowing[j],blend);
    }
    auto braced=Sample(m,idle,0);
    if(s.phase==RuneBloodlust::Charging){
        int run=Sequence(m,weapon?weapon->GetName("A_Forward"):NameString("MOV_ALL_run1_AA0N"));
        if(run<0)run=Sequence(m,"MOV_ALL_run1_AA0S");
        if(run>=0)braced=Sample(m,run,s.age*m.Sequences[run].Info.Rate*1.8f/m.Sequences[run].Info.NumFrames,true);
    }
    const auto meshToActor=Coords::Rotation(m.RotationOffset).ToMatrix();const auto actorToMesh=InverseAffine(meshToActor);
    auto offset=[&](vec3 v){return (actorToMesh*vec4(v,0)).xyz();};
    int chest=Joint(m,"chest"),neck=Joint(m,"neck"),head=Joint(m,"head"),lw=Joint(m,"lwrist"),rw=Joint(m,"rwrist");
    auto bones=m.JointMatrices(braced);
    auto pitch=actorToMesh*mat4::rotate(.18f,0,1,0)*meshToActor;
    Rotate(m,braced,chest,pitch*bones[chest],bones);
    bones=m.JointMatrices(braced);Rotate(m,braced,neck,(actorToMesh*mat4::rotate(.27f,0,1,0)*meshToActor)*bones[neck],bones);
    bones=m.JointMatrices(braced);Rotate(m,braced,head,(actorToMesh*mat4::rotate(.13f,0,1,0)*meshToActor)*bones[head],bones);
    bones=m.JointMatrices(braced);auto chestPoint=JointPoint(bones[chest]);
    auto reference=m.JointMatrices(Sample(m,guard,.125f));
    // Preserve the authored shield grip orientation. Centre it ahead of the
    // chest, then solve both arms to the shield's rear grip area.
    // Shield meshes lie in attachment XY, with their decorated face toward -Z.
    // Keep that face vertical and facing the rush, not the sideways slash pose.
    auto facing=mat4::identity();
    facing[0]=0;facing[1]=-1;facing[2]=0;
    facing[4]=0;facing[5]=0;facing[6]=1;
    facing[8]=-1;facing[9]=0;facing[10]=0;
    auto shieldTarget=At(actorToMesh*facing,chestPoint+offset(vec3(20,0,1)));
    auto leftTarget=shieldTarget*InverseAffine(InverseAffine(reference[lw])*reference[shieldJoint]);
    Arm(m,braced,true,leftTarget,chestPoint+offset(vec3(6,-24,-9)));
    bones=m.JointMatrices(braced);
    // Use the solved left grip so short arms never separate from their shield.
    auto leftGrip=bones[lw];auto rightPosition=JointPoint(leftGrip)+offset(vec3(0,6,0));
    auto mirror=mat4::scale(-1,1,1);auto rightTarget=At(mirror*leftGrip*mirror,rightPosition);
    Arm(m,braced,false,rightTarget,chestPoint+offset(vec3(6,24,-9)));
    // Shield attachment is animated independently from the wrist. Drive it
    // from the same target as the hands so a neutral/run child rotation cannot
    // tilt it back into the original one-handed guard.
    bones=m.JointMatrices(braced);
    braced[shieldJoint]=Local(InverseAffine(bones[m.Joints[shieldJoint].Parent])*shieldTarget);
    for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(pose[j],braced[j],brace);
    if(weapon && !weapon->bDeleteMe() && weapon->Owner()==p && weapon->GetUObject("AttachParent")==p){
        // The real selected weapon follows a temporary stow attachment frame.
        // Inventory/slot links stay unchanged, so cancellation, death and load
        // cannot lose a weapon, displace a spare, or restore a phantom copy.
        int hand=weapon->GetByte("AttachParentJoint"),hip=Joint(m,type==0?"attatch_sword":type==1?"attach_hammer":"attach_axe");
        if(hip<0)hip=Joint(m,"attach_hammer");
        if(hand==Joint(m,"attach_hand") && hip>=0){
            bones=m.JointMatrices(pose);auto parked=bones[hip];
            auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));
            if(children[hip] && children[hip]!=weapon)parked=parked*mat4::translate(5,0,0);
            auto local=Local(InverseAffine(bones[m.Joints[hand].Parent])*parked);
            float parkedBlend=windup?Smooth((s.age-.12f)/.12f):recovery?1-Smooth((s.age-.18f)/.16f):1;
            pose[hand]=BlendRunePose(pose[hand],local,parkedBlend);
        }
    }
}
}

namespace RuneCorpse {
Array<RuneJointPose> ReferencePose(const USkelModel& m) {
    int reference=-1;
    for(auto name:{"neutral_idle","idle","w_idleA","IDL_ALL_base1_AN0N"}) {
        reference=RuneRushPose::Sequence(m,name);if(reference>=0)break;
    }
    if(reference<0)for(size_t i=0;i<m.Sequences.size();++i) {
        auto name=m.Sequences[i].Info.Name.ToString();for(char& c:name)c=char(std::tolower((unsigned char)c));
        if(name.find("idle")!=std::string::npos){reference=int(i);break;}
    }
    return m.DecodeFrame(reference>=0?reference:0,0);
}
Array<RuneJointPose> RelaxedPose(const USkelModel& m) {
    using namespace RuneRushPose;
    auto pose=ReferencePose(m);if(pose.empty())return pose;
    const auto meshToActor=Coords::Rotation(m.RotationOffset).ToMatrix();
    auto actorToMesh=InverseAffine(meshToActor);
    auto direction=[&](vec3 v){return (actorToMesh*vec4(v,0)).xyz();};
    // Lay a proportion-preserving reference pose across the carrier. Root uses
    // the Rune-specific transposed rotation convention; never rescale bones.
    auto laid=actorToMesh*mat4::rotate(1.57079632679f,0,1,0)*meshToActor*
        mat4::transpose(RunePoseRotation(pose[0].Rotation));
    pose[0].Rotation=RunePoseEuler(mat4::transpose(laid));
    auto aim=[&](int joint,int child,vec3 actorDirection) {
        if(joint<0 || child<0 || m.Joints[joint].Parent<0)return;
        auto bones=m.JointMatrices(pose);auto from=JointPoint(bones[child])-JointPoint(bones[joint]);
        Rotate(m,pose,joint,Turn(from,direction(actorDirection))*bones[joint],bones);
    };
    for(bool left:{true,false}) {
        int shoulder=Joint(m,left?"lshouldb":"rshouldb");if(shoulder<0)shoulder=Joint(m,left?"lshould":"rshould");
        int elbow=Joint(m,left?"lelbow":"relbow"),wrist=Joint(m,left?"lwrist":"rwrist");float side=left?1.f:-1.f;
        aim(shoulder,elbow,{.08f,side*.27f,-1.f});
        aim(elbow,wrist,{.24f,side*.14f,-1.f});
        int hip=Joint(m,left?"lhip":"rhip"),knee=Joint(m,left?"lknee":"rknee"),ankle=Joint(m,left?"lankle":"rankle");
        aim(hip,knee,{-1.f,side*.09f,-.38f});
        aim(knee,ankle,{-.18f,side*.07f,-1.f});
    }
    int neck=Joint(m,"neck");if(neck>=0 && m.Joints[neck].Parent>=0) {
        auto bones=m.JointMatrices(pose);auto tilt=actorToMesh*mat4::rotate(.2f,0,1,0)*meshToActor;
        Rotate(m,pose,neck,tilt*bones[neck],bones);
    }
    return pose;
}
void ResetMotion(UActor* actor) {
    auto& s=actor->CorpseBody;s.motionReady=false;s.motionPose.clear();s.limbs.clear();
}
namespace {
void CachePose(const USkelModel& m,UActor* actor) {
    auto& s=actor->CorpseBody;
    if(s.poseModel!=&m || s.pose.size()!=m.Joints.size()) {
        ResetMotion(actor);s.pose=RelaxedPose(m);s.poseModel=&m;
    }
}
}
void TickMotion(UActor* actor,float elapsed) {
    using namespace RuneRushPose;
    if(!actor || actor->bDeleteMe())return;
    auto& s=actor->CorpseBody;
    if(!Posed(actor)){if(s.motionReady)ResetMotion(actor);return;}
    if(!std::isfinite(elapsed) || elapsed<=0)return;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));if(!model)return;
    const auto& m=*model;CachePose(m,actor);
    // These endpoints are local presentation only. Collision, damage, saves and
    // snapshots continue to use the unchanged authoritative rigid body.
    auto world=MeshToWorld(actor);
    if(s.motionReady && (elapsed>.15f || length(JointPoint(world)-JointPoint(s.motionWorld))>100.f))ResetMotion(actor);
    if(!s.motionReady) {
        s.motionPose=s.pose;s.motionWorld=world;auto bones=m.JointMatrices(s.pose);
        auto add=[&](const char* joint,const char* child,float cone,float spring) {
            int j=Joint(m,joint),c=Joint(m,child);
            if(j<0 || c<0 || m.Joints[j].Parent<0 || m.Joints[c].Parent!=j)return;
            LimbMotion node;node.joint=j;node.child=c;node.cone=cone;node.spring=spring;
            node.anchor=JointPoint(world*bones[j]);node.tip=JointPoint(world*bones[c]);
            // Preserve existing travel speed at a late join rather than giving
            // the flying corpse an artificial zero-velocity limb impulse.
            auto* parent=UObject::TryCast<UActor>(actor->GetUObject("AttachParent"));
            node.velocity=parent?parent->Velocity():actor->Velocity();s.limbs.push_back(node);
        };
        for(bool left:{true,false}) {
            auto shoulder=left?"lshouldb":"rshouldb";
            if(Joint(m,shoulder)<0)shoulder=left?"lshould":"rshould";
            add(shoulder,left?"lelbow":"relbow",1.1f,14);
            add(left?"lelbow":"relbow",left?"lwrist":"rwrist",1.f,10);
            add(left?"lhip":"rhip",left?"lknee":"rknee",.22f,45);
            add(left?"lknee":"rknee",left?"lankle":"rankle",.75f,14);
        }
        add("neck","head",.4f,28);s.motionReady=true;
    }
    // At most 18 small steps after a hitch; higher presentation rates use a
    // shorter step so the visible pose is never quantized to the physics rate.
    elapsed=std::min(elapsed,.15f);
    int steps=std::max(1,int(std::ceil(elapsed*120.f)));float dt=elapsed/steps;
    auto gravity=actor->Region().Zone?actor->Region().Zone->ZoneGravity():vec3(0,0,-950);
    for(int step=1;step<=steps;++step) {
        float alpha=float(step)/steps;auto at=world;
        for(int k=0;k<16;++k)at[k]=s.motionWorld[k]+(world[k]-s.motionWorld[k])*alpha;
        if(std::abs(mat3::determinant(mat3(at)))<1e-8f){ResetMotion(actor);return;}
        auto inverse=InverseAffine(at);auto pose=s.pose;
        for(auto& node:s.limbs) {
            auto bones=m.JointMatrices(pose);auto anchor=JointPoint(at*bones[node.joint]);
            auto rest=JointPoint(at*bones[node.child]);auto reference=rest-anchor;float size=length(reference);
            if(size<.001f)continue;
            auto anchorVelocity=(anchor-node.anchor)/dt;
            node.velocity=anchorVelocity+(node.velocity-anchorVelocity)*std::exp(-3.2f*dt);
            node.velocity+=((rest-node.tip)*node.spring+gravity)*dt;
            auto relative=node.velocity-anchorVelocity;float speed=length(relative);
            if(speed>600)node.velocity=anchorVelocity+relative*(600/speed);
            auto previous=node.tip;auto displacement=node.tip+node.velocity*dt-anchor;
            auto axis=reference/size;auto direction=length(displacement)>.001f?normalize(displacement):axis;
            float cosine=std::clamp(dot(axis,direction),-1.f,1.f),limit=std::cos(node.cone);
            if(cosine<limit) {
                auto tangent=direction-axis*cosine;
                direction=length(tangent)>.001f?axis*limit+normalize(tangent)*std::sin(node.cone):axis;
            }
            node.tip=anchor+direction*size;node.velocity=(node.tip-previous)/dt;node.anchor=anchor;
            auto from=JointPoint(bones[node.child])-JointPoint(bones[node.joint]);
            auto target=(inverse*vec4(direction,0)).xyz();
            Rotate(m,pose,node.joint,Turn(from,target)*bones[node.joint],bones);
        }
        s.motionPose=std::move(pose);
    }
    s.motionWorld=world;
    // Limb tips can leave the initial hanging-pose box. Refresh the local
    // bounds from the same cached pose that every draw pass will consume.
    model->PrepareMesh(actor->GetByte("SkelMesh"),actor->AnimSequence(),actor->AnimFrame(),actor);
    vec3 low(1e20f),high(-1e20f);bool any=false;
    auto* groups=static_cast<int*>(actor->GetProperty("SkelGroupFlags"));
    for(const auto& triangle:model->Tris)if(!(groups[triangle.TextureIndex]&PF_Invisible))for(auto index:triangle.Indices)
        if(size_t(index)<model->Verts.size()) {
            auto v=model->Verts[index];for(int k=0;k<3;++k){low[k]=std::min(low[k],v[k]);high[k]=std::max(high[k],v[k]);}any=true;
        }
    if(any){s.lower=low;s.upper=high;}
}
void TickMotion(float elapsed) {
    if(!engine || !engine->LaunchInfo.IsRune() || !engine->Level || elapsed<=0)return;
    for(auto* actor:engine->Level->Actors)
        if(actor && (Posed(actor) || actor->CorpseBody.motionReady))TickMotion(actor,elapsed);
}
void ApplyPose(const USkelModel& m,UActor* actor,Array<RuneJointPose>& pose) {
    if(Posed(actor)) {
        auto& state=actor->CorpseBody;
        CachePose(m,actor);
        pose=state.motionReady && state.motionPose.size()==state.pose.size()?state.motionPose:state.pose;return;
    }
    auto* p=UObject::TryCast<UPawn>(actor);if(!p || p->Health()<=0 || (!Held(p)&&!RunePhysics::Carrying(p)) || !RuneRushPose::Supported(p))return;
    // Reuse the two-bone solver: retain leg animation, bone lengths and scales.
    // Hands support the body above the head; selected equipment uses temporary
    // attachment frames, leaving ownership and all inventory slots untouched.
    using namespace RuneRushPose;
    auto bones=m.JointMatrices(pose);int head=Joint(m,"head");
    auto actorToMesh=InverseAffine(Coords::Rotation(m.RotationOffset).ToMatrix());
    auto offset=[&](vec3 v){return (actorToMesh*vec4(v,0)).xyz();};
    auto top=JointPoint(bones[head])+offset(vec3(7+16*Windup(p),0,17-4*Windup(p)));
    auto relaxed=Sample(m,Sequence(m,"neutral_idle"),0);
    for(bool left:{true,false}) {
        int wrist=Joint(m,left?"lwrist":"rwrist");
        // The carry is an overhead palm-up grip, not the world-space hand
        // rotation from the standing/run animation (which folds hands into
        // the forearms/body when the elbows are raised).
        auto fingers=normalize(vec3(.20f,left?1.f:-1.f,.08f));
        auto y=-fingers,x=normalize(cross(y,vec3(0,0,1))),z=cross(x,y);
        auto grip=mat4::identity();for(int k=0;k<3;++k){grip[k]=x[k];grip[4+k]=y[k];grip[8+k]=z[k];}
        auto target=At(actorToMesh*grip,top+offset(vec3(0,left?-18.f:18.f,0)));
        Arm(m,pose,left,target,top+offset(vec3(15,left?-35.f:35.f,-24)));
        int hand=Joint(m,left?"lhand":"rhand");if(hand>=0)pose[hand].Rotation=relaxed[hand].Rotation;
    }
    auto park=[&](UActor* item,int destination) {
        if(!item || item->bDeleteMe() || item->GetUObject("AttachParent")!=p || destination<0)return;
        int j=item->GetByte("AttachParentJoint");if(j>=int(m.Joints.size()) || m.Joints[j].Parent<0)return;
        bones=m.JointMatrices(pose);auto frame=bones[destination];
        auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));
        if(children[destination] && children[destination]!=item)frame=frame*mat4::translate(5,0,0);
        pose[j]=Local(InverseAffine(bones[m.Joints[j].Parent])*frame);
    };
    if(auto* w=p->Weapon())park(w,Joint(m,w->GetByte("MeleeType")==0?"attatch_sword":w->GetByte("MeleeType")==1?"attach_hammer":"attach_axe"));
    auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    if(shield && shield->GetUObject("AttachParent")==p) {
        int j=shield->GetByte("AttachParentJoint"),chest=Joint(m,"chest");
        if(j<int(m.Joints.size()) && m.Joints[j].Parent>=0) {
            bones=m.JointMatrices(pose);auto back=At(bones[chest],JointPoint(bones[chest])+offset(vec3(-13,0,0)));
            pose[j]=Local(InverseAffine(bones[m.Joints[j].Parent])*back);
        }
    }
}
}
