#include "Animation/RuneIKMath.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <random>
using namespace RuneIK;
int main(){try{
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(why);};
 std::mt19937 random(17391);std::uniform_real_distribution<float>d(-100,100),len(.01f,50);unsigned cases=0;float worst=0;
 for(unsigned i=0;i<40000;++i){vec3 root(d(random),d(random),d(random));float a=len(random),b=len(random);vec3 joint=root+vec3(a,0,0),end=joint+vec3(b*.6f,b*.8f,0),target(root.x+d(random),root.y+d(random),root.z+d(random));
  auto out=TwoBone(root,joint,end,target,root+vec3(0,40,15));check(out.valid,"finite IK result");float error=std::max(std::abs(length(out.joint-root)-a),std::abs(length(out.end-out.joint)-b));worst=std::max(worst,error);if(error>=.004f)std::cerr<<"i="<<i<<" a="<<a<<" b="<<b<<" requested="<<length(target-root)<<" error="<<error<<" actual="<<length(out.joint-root)<<","<<length(out.end-out.joint)<<"\n";check(error<.004f,"IK preserves both bone lengths");
  float requested=length(target-root);if(requested>std::abs(a-b)+.01f&&requested<a+b-.01f)check(out.error<.002f,"reachable effector reaches target");else check(std::abs(out.error-std::max({0.f,requested-a-b,std::abs(a-b)-requested}))<.004f,"unreachable target clamps without stretching");++cases;
 }
 auto aligned=TwoBone({0,0,0},{10,0,0},{20,0,0},{15,0,0},{30,0,0});check(aligned.valid&&std::abs(length(aligned.joint)-10)<.001f,"collinear pole remains finite");
 auto folded=TwoBone({0,0,0},{10,0,0},{0,0,0},{0,0,0},{0,1,0});check(folded.valid,"target at root remains finite");
 check(!TwoBone({0,0,0},{0,0,0},{1,0,0},{2,0,0},{0,1,0}).valid,"zero bone rejected");
 auto bad=vec3(std::numeric_limits<float>::quiet_NaN(),0,0);check(!TwoBone({0,0,0},{1,0,0},{2,0,0},bad,{0,1,0}).valid,"nonfinite target rejected");
 std::cout<<"CHARACTER_IK_MATH_OK cases="<<cases<<" max_length_error="<<worst<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
