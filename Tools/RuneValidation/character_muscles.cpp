#include "Physics/RunePhysicsWorld.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace RuneRigid;
float angle(Rotation a,Rotation b){float d=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);return 2*std::acos(std::clamp(d,0.f,1.f));}
int main(){try{
 auto check=[](bool b,const char*w){if(!b)throw std::runtime_error(w);};
 for(float dt:{1.f/60,1.f/120}) {World w;w.gravity({0,0,-980});
 Description d;d.position={0,0,150};d.group=11;d.mass=6;d.capsule=true;d.radius=4;d.halfHeight=8;d.motion=Motion::Kinematic;auto root=w.create(d);
 d.motion=Motion::Kinematic;d.position={0,20,150};auto limb=w.create(d);w.motion(limb,Motion::Dynamic);w.constrain(root,limb,{0,10,150},{0,1,0},1.4f,.8f);
 Rotation target{std::sin(.25f),0,0,std::cos(.25f)};
 check(w.drive(limb,target,8,1,120),"muscle accepts target");float worstStretch=0;
 for(int i=0;i<1200;++i){check(w.drive(limb,target,8,1,120),"drive persists");w.step(dt);auto a=w.get(root),b=w.get(limb);check(std::isfinite(b.position.z)&&std::isfinite(b.rotation.w),"driven joint remains finite");worstStretch=std::max(worstStretch,length(a.position-b.position));}
 float error=angle(multiply(inverse(w.get(root).rotation),w.get(limb).rotation),target);std::cout<<"drive_error="<<error<<" stretch="<<worstStretch<<"\n";check(error<.12f&&worstStretch<26,"muscle holds authored pose against gravity");
 w.impulse(limb,{1800,0,1600},w.get(limb).position);float response=0;
 for(int i=0;i<900;++i){w.drive(limb,target,8,1,120);w.step(dt);response=std::max(response,angle(w.get(limb).rotation,target));}
 check(response>.05f,"muscle responds physically to impact");check(angle(w.get(limb).rotation,target)<.12f,"muscle settles back after impact");
 check(!w.drive(limb,{0,0,0,0},8,1,120),"invalid target rejected");check(!w.drive(limb,target,std::numeric_limits<float>::infinity(),1,120),"invalid frequency rejected");
 check(w.releaseDrive(limb),"muscle releases to passive ragdoll");for(int i=0;i<300;++i)w.step(dt);check(angle(w.get(limb).rotation,target)>.2f,"released limb is free to fall");
 check(w.drive(limb,target,8,1,120),"muscle can reactivate");for(int i=0;i<900;++i){w.drive(limb,target,8,1,120);w.step(dt);}check(angle(w.get(limb).rotation,target)<.12f,"muscle recovers after unpinning");
 w.remove(root);check(!w.drive(limb,target,8,1,120),"removed parent clears drive handle");w.remove(limb);check(!w.releaseDrive(limb),"removed limb never leaves stale motor");
 // Follow a translating and rotating kinematic animation root at the actual
 // 60 Hz physics cadence as well as 120 Hz. Relaxed limits must follow a pose
 // outside the original joint bind cone without numerical instability.
 d.motion=Motion::Kinematic;d.position={0,0,250};d.rotation={};auto moving=w.create(d);
 d.motion=Motion::Dynamic;d.position={0,20,250};auto follower=w.create(d);w.constrain(moving,follower,{0,10,250},{0,1,0},.2f,.1f);
 Rotation wide{std::sin(.6f),0,0,std::cos(.6f)};
 float maxLength=0;
 for(int i=0;i<900;++i){float t=i*dt;Rotation yaw{0,0,std::sin(.1f*t),std::cos(.1f*t)};w.kinematic(moving,{40*std::sin(t),0,250},yaw,dt);check(w.drive(follower,wide,10,1,240,true),"moving-root motor");w.step(dt);auto r=w.get(moving),v=w.get(follower);maxLength=std::max(maxLength,length(v.position-r.position));check(std::isfinite(v.rotation.w),"moving puppet finite");}
 check(maxLength<30,"moving-root joint retains attachment");check(angle(multiply(inverse(w.get(moving).rotation),w.get(follower).rotation),wide)<.16f,"muscle follows target beyond passive bind limits");w.remove(moving);w.remove(follower);
 }
 {World w;w.gravity({0,0,0});Description d;d.position={0,0,30};d.radius=10;d.sphere=true;d.cosmetic=true;d.motion=Motion::Kinematic;auto cosmetic=w.create(d);w.motion(cosmetic,Motion::Dynamic);d.cosmetic=false;d.position={3,0,30};auto gameplay=w.create(d);for(int i=0;i<120;++i)w.step(1.f/60);check(length(w.get(gameplay).position-d.position)<.001f,"cosmetic puppet never displaces gameplay body");}
 std::cout<<"CHARACTER_MUSCLES_OK active=1 impulse=1 release=1 recovery=1\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
