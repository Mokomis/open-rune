#include "openrune/net/Prediction.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace openrune::net;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(0)
Snapshot state(uint64_t tick,uint64_t input,uint16_t held,float x,uint32_t life=1)
{
    Entity e;e.ref={1,1};e.life=life;e.flags=3;e.health=100;e.position[0]=int32_t(x*8);
    Snapshot s{tick,0,tick,{e}};s.movement=MovementAck{e.ref,life,input,held,1,800,2048,2048,256};return s;
}
int main()
{
 try {
    size_t calls=0;
    auto step=[&](MovementPose& p,const Input& i,const MovementAck&,float dt) {
        ++calls;p.velocity[0]=float(i.forward)/10;p.position[0]+=p.velocity[0]*dt;return true;
    };
    MovementPrediction p;Input i;i.sequence=1;i.life=1;i.forward=1000;
    CHECK(p.sample(i,1000));CHECK(p.checkpoint(state(1,1,1,2),{1,1},1100,step));CHECK(p.active());
    auto pose=p.present(1100,step);CHECK(std::abs(pose.position[0]-10.3334f)<.002f);
    auto count=calls;auto bad=state(2,2,1,4);CHECK(!p.checkpoint(bad,{1,1},1100,step));CHECK(calls==count);
    CHECK(!p.sample(i,999));i.sequence=2;i.forward=-1000;CHECK(p.sample(i,1100));
    pose=p.present(1117,step);CHECK(pose.position[0]<9 && pose.position[0]>8);
    CHECK(p.historySize()==2);
    // Receipt of an input does not change base state; only paired checkpoints do.
    CHECK(p.stats().checkpoints==1);
    auto s=state(2,2,1,9);CHECK(p.checkpoint(s,{1,1},1130,step));CHECK(p.historySize()==1);
    // No speculative movement beyond the budget during outage.
    auto late=p.present(1600,step);auto later=p.present(3000,step);
    CHECK(std::abs(late.position[0]-later.position[0])<2);CHECK(p.stats().limited>0);
    // Death/respawn cannot inherit corrections, pending input or old-life replay.
    i.life=2;i.sequence=3;CHECK(p.sample(i,3000));s=state(3,3,1,1000,2);
    CHECK(p.checkpoint(s,{1,1},3030,step));pose=p.present(3030,step);CHECK(pose.position[0]>998 && pose.position[0]<1000);
    s=state(4,3,2,500,2);s.world[0].health=0;s.world[0].flags=2;s.movement->mode=0;
    CHECK(p.checkpoint(s,{1,1},3050,step));CHECK(!p.active());CHECK(p.present(3060,step).position[0]==500);
    // Unmatched identity, stale checkpoint and invalid parameter bounds are atomic.
    CHECK(!p.checkpoint(s,{2,1},3060,step));CHECK(!p.checkpoint(s,{1,1},3060,step));
    auto invalid=state(5,3,1,500,2);invalid.movement->mode=2;CHECK(!p.checkpoint(invalid,{1,1},3060,step));
    Bytes bytes;Snapshot decoded;auto wire=state(6,3,2,500,2);
    CHECK(encodeSnapshot(wire,nullptr,bytes));CHECK(decodeSnapshot(bytes,nullptr,decoded));CHECK(decoded==wire);
    for(size_t n=0;n<bytes.size();++n)CHECK(!decodeSnapshot(std::span(bytes).first(n),nullptr,decoded));
    wire.movement->life=9;CHECK(!encodeSnapshot(wire,nullptr,bytes));
    // Falling is explicit, authority-granted, with its own bounded parameters.
    auto falling=state(7,3,2,500,2);falling.world[0].flags=1;
    falling.movement->mode=MovementFalling;falling.movement->acceleration=0;
    falling.movement->friction=950;falling.movement->step=20000;
    CHECK(encodeSnapshot(falling,nullptr,bytes));CHECK(decodeSnapshot(bytes,nullptr,decoded) && decoded==falling);
    auto badAir=falling;badAir.world[0].flags=3;CHECK(!validMovementAck(badAir.movement,badAir.world));
    badAir=falling;badAir.movement->friction=0;CHECK(!validMovementAck(badAir.movement,badAir.world));
    badAir=falling;badAir.movement->mode=3;CHECK(!validMovementAck(badAir.movement,badAir.world));
    {
        MovementPrediction jump;Input input;input.sequence=1;input.life=1;input.forward=1000;
        auto airStep=[](MovementPose& p,const Input& i,const MovementAck& a,float dt) {
            p.velocity[0]=float(i.forward)/10;p.position[0]+=p.velocity[0]*dt;
            if(a.mode==MovementFalling){p.velocity[2]=200;p.position[2]+=200*dt;}return true;
        };
        CHECK(jump.sample(input,1000));CHECK(jump.checkpoint(state(1,1,1,0),{1,1},1050,airStep));
        auto before=jump.present(1100,airStep);
        auto takeoff=state(2,1,2,4);takeoff.world[0].flags=1;takeoff.world[0].position[2]=560;
        takeoff.movement->mode=MovementFalling;takeoff.movement->friction=950;takeoff.movement->step=20000;
        CHECK(jump.checkpoint(takeoff,{1,1},1100,airStep));auto start=jump.present(1100,airStep);
        CHECK(std::abs(start.position[2]-before.position[2])<.001f);
        CHECK(jump.present(1116,airStep).position[2]>start.position[2]);
        auto prior=jump.present(1118,airStep);auto burst=takeoff;++burst.id;++burst.tick;
        CHECK(jump.checkpoint(burst,{1,1},1118,airStep));
        CHECK(std::abs(jump.present(1118,airStep).position[2]-prior.position[2])<.001f);
        auto landing=state(4,1,20,10);CHECK(jump.checkpoint(landing,{1,1},1350,airStep));
        CHECK(jump.present(1350,airStep).position[2]==0);
    }
    // Outage/high-rate history remains bounded and a missing marker falls back.
    p.reset();for(uint64_t n=1;n<=1000;++n){i.sequence=n;i.life=1;CHECK(p.sample(i,n*2));}
    CHECK(p.historySize()<=128);CHECK(p.checkpoint(state(1,1,1,3),{1,1},2000,step));CHECK(!p.active());
    // A failing or nonfinite callback cannot corrupt the authoritative base.
    p.reset();i.sequence=1;i.life=1;CHECK(p.sample(i,100));
    auto nan=[](MovementPose& p,const Input&,const MovementAck&,float){p.position[0]=std::numeric_limits<float>::quiet_NaN();return true;};
    CHECK(p.checkpoint(state(1,1,1,4),{1,1},200,nan));CHECK(p.present(200,nan).position[0]==4);
    // Render frequency cannot change replayed position at the same time.
    float expected=0;
    for(unsigned hz:{30,60,90,120,144,165,240}) {
        MovementPrediction test;i.forward=1000;CHECK(test.sample(i,1000));
        CHECK(test.checkpoint(state(1,1,1,0),{1,1},1020,step));
        for(unsigned frame=1;1020+frame*1000/hz<1200;++frame)test.present(1020+frame*1000/hz,step);
        float result=test.present(1200,step).position[0];
        if(expected)CHECK(std::abs(result-expected)<.001f);expected=result;
    }
    std::cout<<"PASS bounded prediction, processed-input checkpoint, replay, correction, life reset, wire and render-rate independence\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
