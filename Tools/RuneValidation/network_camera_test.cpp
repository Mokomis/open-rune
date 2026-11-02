#include "RuneNetworkCamera.h"
#include <cassert>
#include <iostream>
int main(){
 for(int hz:{30,60,120,144,240}){
  RuneNetworkCameraHeight c;float dt=1.f/hz;assert(c.update(0,1,true,dt)==0);float last=0;
  for(int i=0;i<hz;i++){float z=c.update(24,1,true,dt);assert(z>=last && z<=24);if(i==0)assert(z<10);last=z;}
  assert(last>23.9f);assert(c.update(24,2,true,dt)==24);assert(c.update(80,2,false,dt)==80);
  assert(c.update(800,2,true,dt)==800);c.reset();assert(c.update(-50,2,true,dt)==-50);
 }
 // A shared mesh/camera height softens a real-sized 16-unit tread without
 // changing the collision height, overshooting, or carrying lag into a jump.
 for(int hz:{30,60,120,144,240}) {
  RuneNetworkCameraHeight height;float dt=1.f/hz,previous=0,maxRise=0;
  height.update(0,1,true,dt,.045f);
  for(int i=1;i<hz;++i) {
   float physical=16.f*int((double(i)/hz)/.1);
   float visual=height.update(physical,1,true,dt,.045f);
   assert(visual>=previous && visual<=physical && physical-visual<24);
   maxRise=std::max(maxRise,visual-previous);previous=visual;
  }
  assert(maxRise<16);assert(height.update(200,1,false,dt,.045f)==200);
  assert(height.update(0,2,true,dt,.045f)==0);
 }
 std::cout<<"PASS camera step convergence, frame rates, respawn, airborne and teleport reset\n";
}
