#include "RunePose.h"
#include <iostream>
#include <stdexcept>
#include <random>
float error(const mat4& a,const mat4& b){float e=0;for(int i=0;i<16;i++)e=std::max(e,std::abs(a[i]-b[i]));return e;}
int main(){
 std::mt19937 rng(7);float worst=0;
 for(int i=0;i<10000;i++){
  Rotator r{(int)(rng()%65536),(int)(rng()%65536),(int)(rng()%65536)};
  auto m=RunePoseRotation(r);auto rebuilt=RunePoseRotation(RunePoseEuler(m));worst=std::max(worst,error(m,rebuilt));
 }
 std::cout<<"rotation_roundtrip_max="<<worst<<'\n';
 if(worst>.001f)throw std::runtime_error("Rune Euler/matrix conversion mismatch");
 RuneJointPose a,b;a.Rotation={0,32700,0};b.Rotation={0,-32700,0};b.Position={4,6,8};b.Scale=vec3(3);
 auto half=BlendRunePose(a,b,.5f);
 if(length(half.Position-vec3(2,3,4))>.0001f||length(half.Scale-vec3(2))>.0001f)throw std::runtime_error("Position/scale blend mismatch");
 if(error(RunePoseRotation(half.Rotation),RunePoseRotation({0,32768,0}))>.001f)throw std::runtime_error("Rotation crossed long arc");
 for(int i=0;i<1000;i++){
  a.Rotation={(int)(rng()%65536),(int)(rng()%65536),(int)(rng()%65536)};
  b.Rotation={(int)(rng()%65536),(int)(rng()%65536),(int)(rng()%65536)};
  auto m=RunePoseRotation(BlendRunePose(a,b,.5f).Rotation);
  for(int j=0;j<3;j++){vec3 axis=(m*vec4(j==0,j==1,j==2,0)).xyz();if(std::abs(length(axis)-1)>.0001f)throw std::runtime_error("Blend changed bone rotation scale");}
  if(BlendRunePose(a,b,0).Rotation!=a.Rotation||BlendRunePose(a,b,1).Rotation!=b.Rotation)throw std::runtime_error("Blend endpoints changed");
 }
 std::cout<<"RUNE_POSE_MATH_OK shortest_arc=1 endpoints=1 orthonormal=1 samples=11000\n";
}
