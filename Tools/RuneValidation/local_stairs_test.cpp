#include "RuneStairPresentation.h"
#include <cassert>
#include <iostream>
using RuneStairPresentation::Height;
int main() {
    // Independent staircase + first-order camera, both directions and rates.
    // The rendered body must follow the camera between adjacent physical treads.
    int checks=0;
    for(int hz:{30,60,120,144,240})for(float direction:{-1.f,1.f}) {
        Height h;float dt=1.f/hz,anchor=0,lastRelative=0,previousZ=0;
        h.update(0,0,0,0,dt,24,true);
        for(int i=1;i<hz*3;++i) {
            float z=direction*16*std::floor(double(i)/hz/.1);
            anchor+=(z-anchor)*(1-std::exp(-dt*7));
            float off=h.update(float(i)*320/hz,0,z,anchor-z,dt,24,true);
            assert(std::abs(off)<48);
            float relative=z+off-anchor;
            assert(std::abs(relative-lastRelative)<.001f);
            lastRelative=relative;previousZ=z;++checks;
        }
        assert(h.update(1000,0,previousZ,0,dt,24,false)==0); // jump/death/cinematic
    }
    Height h;
    h.update(0,0,0,0,.01f,24,true);
    assert(h.update(2,0,16,-15,.01f,24,true)==-15);
    assert(h.update(2,0,16,-15,0,24,true)==-15); // paused
    assert(h.update(500,0,16,-15,.01f,24,true)==0); // teleport
    assert(h.update(501,0,16,-30,.01f,24,true)==0); // ceiling on flat ground
    assert(h.update(502,0,32,-15,.01f,24,true)==-15);
    assert(h.update(503,0,64,-15,.01f,24,true)==0); // large ledge
    assert(h.update(504,0,80,-15,.01f,24,true)==-15);
    assert(h.update(505,0,96,-15,.2f,24,true)==0); // frame stall
    assert(h.update(506,0,112,-15,.01f,24,true)==-15);
    assert(h.update(507,0,128,-80,.01f,24,true)==0); // invalid camera anchor
    h.reset();assert(h.update(2,0,16,-15,.01f,24,true)==0);
    std::cout<<"LOCAL_STAIRS_OK checks="<<checks<<" rates=5 reset_cases=8\n";
}
