#include "Precomp.h"
#include "RuneRushPose.h"
#include "RuneBloodlust.h"
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
vec3 Point(const mat4& m){return {m[12],m[13],m[14]};}
mat4 At(mat4 m,vec3 p){m[12]=p.x;m[13]=p.y;m[14]=p.z;return m;}
mat4 Turn(vec3 a,vec3 b){
    if(length(a)<.001f||length(b)<.001f)return mat4::identity();
    a=normalize(a);b=normalize(b);float d=std::clamp(dot(a,b),-1.f,1.f);
    if(d>.99999f)return mat4::identity();
    if(d<-.99999f){auto axis=cross(a,vec3(1,0,0));if(length(axis)<.01f)axis=cross(a,vec3(0,1,0));axis=normalize(axis);return mat4::quaternion(axis.x,axis.y,axis.z,0);}
    auto axis=cross(a,b);float k=1.f/std::sqrt(2*(1+d));return mat4::quaternion(axis.x*k,axis.y*k,axis.z*k,(1+d)*k);
}
RuneJointPose Local(const mat4& m){
    RuneJointPose p;p.Position=Point(m);mat4 rotation=m;
    for(int c=0;c<3;++c){p.Scale[c]=length(vec3(m[c*4],m[c*4+1],m[c*4+2]));if(p.Scale[c]>.001f)for(int r=0;r<3;++r)rotation[c*4+r]/=p.Scale[c];}
    p.Rotation=RunePoseEuler(rotation);return p;
}
void Rotate(const USkelModel& m,Array<RuneJointPose>& pose,int j,const mat4& worldRotation,const Array<mat4>& bones){
    auto desired=At(worldRotation,Point(bones[j]));
    auto local=Local(InverseAffine(bones[m.Joints[j].Parent])*desired);
    pose[j].Rotation=local.Rotation; // retain actual bone lengths and scales
}
void Arm(const USkelModel& m,Array<RuneJointPose>& pose,bool left,const mat4& wristTarget,vec3 pole){
    int shoulder=Joint(m,left?"lshouldb":"rshouldb"),elbow=Joint(m,left?"lelbow":"relbow"),wrist=Joint(m,left?"lwrist":"rwrist");
    auto bones=m.JointMatrices(pose);auto start=Point(bones[shoulder]),target=Point(wristTarget);
    float a=length(pose[elbow].Position),b=length(pose[wrist].Position);
    if(a<.01f||b<.01f||length(target-start)<.01f)return;
    auto direction=normalize(target-start);float distance=std::clamp(length(target-start),std::abs(a-b)+.01f,a+b-.01f);
    target=start+direction*distance;pole-=start;pole-=direction*dot(pole,direction);if(length(pole)<.01f)return;
    float cosine=std::clamp((a*a+distance*distance-b*b)/(2*a*distance),-1.f,1.f);
    auto bend=start+direction*(cosine*a)+normalize(pole)*(std::sqrt(1-cosine*cosine)*a);
    Rotate(m,pose,shoulder,Turn(Point(bones[elbow])-start,bend-start)*bones[shoulder],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,elbow,Turn(Point(bones[wrist])-Point(bones[elbow]),target-Point(bones[elbow]))*bones[elbow],bones);
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
    bones=m.JointMatrices(braced);auto chestPoint=Point(bones[chest]);
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
    auto leftGrip=bones[lw];auto rightPosition=Point(leftGrip)+offset(vec3(0,6,0));
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
