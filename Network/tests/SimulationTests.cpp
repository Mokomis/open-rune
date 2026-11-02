#include "openrune/net/Simulation.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace openrune::net;
#define CHECK(v) do { if(!(v))throw std::runtime_error(#v); } while(0)
int main()
{
 try {
    for(uint64_t display:{30,60,120,144,165,240}) {
        FixedSimulation c; uint64_t lastTick=0;double simulated=0;
        auto tick=[&](uint64_t n,float dt){CHECK(n==++lastTick);CHECK(dt==1.0f/60);simulated+=dt;};
        for(uint64_t frame=0;frame<=display*10;++frame)CHECK(c.advance(frame*1000000/display,tick));
        CHECK(lastTick==600 && c.ticks()==600 && c.droppedTicks()==0);CHECK(std::abs(simulated-10)<0.00001);
        CHECK(!c.advance(1,tick));CHECK(c.ticks()==600);
    }
    FixedSimulation stalled; auto ignore=[](uint64_t,float){};CHECK(stalled.advance(0,ignore));CHECK(stalled.advance(5000000,ignore));
    CHECK(stalled.ticks()==4 && stalled.droppedTicks()==296);CHECK(stalled.advance(5000001,ignore));CHECK(stalled.ticks()==4);
    CHECK(!stalled.advance(UINT64_MAX,ignore));
    EntityRegistry registry;CHECK(!registry.bind(0));auto first=registry.bind(1);CHECK(first);CHECK(registry.bind(1)==first);
    CHECK(registry.resolve(*first)==1);CHECK(registry.release(1));CHECK(!registry.resolve(*first));
    auto reused=registry.bind(1);CHECK(reused && reused!=first);CHECK(!registry.resolve({reused->id,2}));
    for(uintptr_t i=2;i<=512;++i)CHECK(registry.bind(i));CHECK(!registry.bind(513));CHECK(registry.size()==512);
    CHECK(quantizePosition(-0.125)==-1);CHECK(quantizePosition(16777216)==134217728);CHECK(!quantizePosition(16777217));
    CHECK(!quantizePosition(std::numeric_limits<double>::quiet_NaN()));CHECK(!quantizePosition(std::numeric_limits<double>::infinity()));
    CHECK(!quantizePosition(1e300));CHECK(quantizeVelocity(8191.75)==32767);CHECK(!quantizeVelocity(8192));
    CHECK(quantizeAnimationRate(-2)==-512);
    Entity a,b;a.ref=b.ref={1,1};a.position={0,0,0};b.position={800,0,0};a.angles[1]=65000;b.angles[1]=536;
    auto p=interpolatePose(a,b,.5);CHECK(p.position[0]==50);CHECK(p.angles[1]==0);
    b.ref.generation=2;p=interpolatePose(a,b,.2);CHECK(p.position[0]==100);
    p=interpolatePose(a,b,std::numeric_limits<double>::quiet_NaN());CHECK(p.position[0]==100);
    b.ref=a.ref;a.bodyAnimation=b.bodyAnimation=3;a.torsoAnimation=b.torsoAnimation=4;
    a.animationPhase=8192;b.animationPhase=24575;a.torsoPhase=3277;b.torsoPhase=9830;
    a.animationRate=256;b.animationRate=512;a.torsoRate=512;b.torsoRate=1024;
    p=interpolatePose(a,b,.5);CHECK(std::abs(p.bodyPhase-.5)<.001 && std::abs(p.torsoPhase-.2)<.001);
    CHECK(p.bodyRate==1.5 && p.torsoRate==3);
    a.flags=b.flags=24;a.animationPhase=31128;b.animationPhase=1638;
    p=interpolatePose(a,b,.5);CHECK(p.bodyPhase<.002 || p.bodyPhase>.998);
    a.flags=b.flags=0;p=interpolatePose(a,b,.5);CHECK(std::abs(p.bodyPhase-.05)<.001);
    a.animationPhase=-8192;b.animationPhase=0;p=interpolatePose(a,b,.5);CHECK(std::abs(p.bodyPhase+.125)<.001);
    b.bodyAnimation=5;p=interpolatePose(a,b,.5);CHECK(p.bodyPhase==0);
    b.life=2;p=interpolatePose(a,b,.5);CHECK(p.position[0]==100);
    std::cout<<"PASS fixed tick, overload, identity reuse, quantization and interpolation\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
