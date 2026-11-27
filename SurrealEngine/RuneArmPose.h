#pragma once
#include "Animation/RuneIKMath.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
namespace RuneArmPose {
inline mat4 InverseAffine(const mat4& m){
    auto inv=mat3::inverse(mat3(m));auto out=mat4::identity();
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)out[c*4+r]=inv[c*3+r];
    auto t=inv*vec3(-m[12],-m[13],-m[14]);out[12]=t.x;out[13]=t.y;out[14]=t.z;return out;
}
inline int Joint(const USkelModel& m,const char* name){for(size_t i=0;i<m.Joints.size();++i)if(m.Joints[i].Name==name)return (int)i;return -1;}
inline float Smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
inline vec3 JointPoint(const mat4& m){return {m[12],m[13],m[14]};}
inline mat4 At(mat4 m,vec3 p){m[12]=p.x;m[13]=p.y;m[14]=p.z;return m;}
inline mat4 Turn(vec3 a,vec3 b){
    if(length(a)<.001f||length(b)<.001f)return mat4::identity();
    a=normalize(a);b=normalize(b);float d=std::clamp(dot(a,b),-1.f,1.f);
    if(d>.99999f)return mat4::identity();
    if(d<-.99999f){auto axis=cross(a,vec3(1,0,0));if(length(axis)<.01f)axis=cross(a,vec3(0,1,0));axis=normalize(axis);return mat4::quaternion(axis.x,axis.y,axis.z,0);}
    auto axis=cross(a,b);float k=1.f/std::sqrt(2*(1+d));return mat4::quaternion(axis.x*k,axis.y*k,axis.z*k,(1+d)*k);
}
inline RuneJointPose Local(const mat4& m){
    RuneJointPose p;p.Position=JointPoint(m);mat4 rotation=m;
    for(int c=0;c<3;++c){p.Scale[c]=length(vec3(m[c*4],m[c*4+1],m[c*4+2]));if(p.Scale[c]>.001f)for(int r=0;r<3;++r)rotation[c*4+r]/=p.Scale[c];}
    p.Rotation=RunePoseEuler(rotation);return p;
}
inline void Rotate(const USkelModel& m,Array<RuneJointPose>& pose,int j,const mat4& worldRotation,const Array<mat4>& bones){
    auto desired=At(worldRotation,JointPoint(bones[j]));
    auto local=Local(InverseAffine(bones[m.Joints[j].Parent])*desired);
    pose[j].Rotation=local.Rotation; // retain actual bone lengths and scales
}
inline void Arm(const USkelModel& m,Array<RuneJointPose>& pose,bool left,const mat4& wristTarget,vec3 pole,int shoulderOverride=-1){
    int shoulder=shoulderOverride>=0?shoulderOverride:Joint(m,left?"lshouldb":"rshouldb"),elbow=Joint(m,left?"lelbow":"relbow"),wrist=Joint(m,left?"lwrist":"rwrist");
    if(shoulder<1 || elbow<1 || wrist<1)return;
    auto bones=m.JointMatrices(pose);auto start=JointPoint(bones[shoulder]),target=JointPoint(wristTarget);
    auto solution=RuneIK::TwoBone(start,JointPoint(bones[elbow]),JointPoint(bones[wrist]),target,pole);
    if(!solution.valid)return;
    target=solution.end;auto bend=solution.joint;
    Rotate(m,pose,shoulder,Turn(JointPoint(bones[elbow])-start,bend-start)*bones[shoulder],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,elbow,Turn(JointPoint(bones[wrist])-JointPoint(bones[elbow]),target-JointPoint(bones[elbow]))*bones[elbow],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,wrist,wristTarget,bones);
}

}
