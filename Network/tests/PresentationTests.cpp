#include "openrune/net/Presentation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace openrune::net;
#define CHECK(...) do{if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(0)
Snapshot world(uint64_t id,uint64_t tick)
{
    Entity e;e.ref={1,1};e.health=100;e.flags=3;e.position[0]=int32_t(tick*8);
    return {id,0,tick,{e}};
}
double position(PresentationTimeline::Frame f)
{
    CHECK(f.before && f.after && f.alpha>=0 && f.alpha<=1);
    return (f.before->world[0].position[0]*(1-f.alpha)+f.after->world[0].position[0]*f.alpha)/8;
}
void profile(unsigned hz,int impairment)
{
    const bool jitter=impairment==1,loss=impairment!=0;
    struct Event { uint64_t at;Snapshot snapshot; };
    std::vector<Event> events;
    constexpr int pattern[]={-18,14,-7,25,-15,4,18,-4};
    for(uint64_t n=0;n<300;++n) {
        if(loss && (n%23==12 || (n%97>=53 && n%97<=55)))continue;
        auto tick=n*2+1;
        int64_t at=int64_t(tick*1000/60+75)+(jitter?pattern[n%8]:0);
        events.push_back({uint64_t(at),world(n+1,tick)});
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.at<b.at;});
    PresentationTimeline timeline;Snapshot previous,current;size_t next=0;
    uint64_t installedAt=0,spacing=33;double lastAdaptive=0,lastLegacy=0,squaredAdaptive=0,squaredLegacy=0;
    size_t samples=0,adaptiveHolds=0,legacyHolds=0;double maxDelay=0;
    for(uint64_t frame=0;frame*1000/hz<10000;++frame) {
        uint64_t now=frame*1000/hz;
        while(next<events.size() && events[next].at<=now) {
            auto incoming=events[next++].snapshot;
            if(current.id && incoming.id<=current.id)continue;
            previous=current;current=incoming;
            if(installedAt)spacing=std::clamp<uint64_t>(now-installedAt,16,150);
            installedAt=now;CHECK(timeline.push(incoming,now));
        }
        if(!current.id)continue;
        auto f=timeline.sample(now);double adaptive=position(f);
        double alpha=std::clamp(double(now-installedAt)/spacing,0.,1.);
        double legacy=(previous.id?previous.world[0].position[0]*(1-alpha)+current.world[0].position[0]*alpha:current.world[0].position[0])/8;
        CHECK(adaptive+1e-7>=lastAdaptive);CHECK(adaptive<=double(current.world[0].position[0])/8);
        maxDelay=std::max(maxDelay,timeline.stats().targetDelayMs);
        CHECK(maxDelay<=PresentationTimeline::MaximumDelayMs && timeline.size()<=PresentationTimeline::Capacity);
        CHECK(timeline.stats().bufferedMs<=PresentationTimeline::MaximumDelayMs+1e-6);
        if(now>2000 && now<9000) {
            double expected=60./hz;
            squaredAdaptive+=std::pow(adaptive-lastAdaptive-expected,2);
            squaredLegacy+=std::pow(legacy-lastLegacy-expected,2);++samples;
            if(std::abs(adaptive-lastAdaptive)<1e-6)++adaptiveHolds;
            if(std::abs(legacy-lastLegacy)<1e-6)++legacyHolds;
        }
        lastAdaptive=adaptive;lastLegacy=legacy;
    }
    std::cout<<"PRESENTATION hz="<<hz<<" profile="<<impairment<<" adaptive_rms="<<std::sqrt(squaredAdaptive/samples)
        <<" legacy_rms="<<std::sqrt(squaredLegacy/samples)<<" adaptive_holds="<<adaptiveHolds<<" legacy_holds="<<legacyHolds<<" max_delay_ms="<<maxDelay<<'\n';
    if(loss)CHECK(squaredAdaptive<squaredLegacy*.65);
    else CHECK(squaredAdaptive/samples<.02);
}
int main()
{
 try {
    PresentationTimeline t;CHECK(!t.sample(0).after);CHECK(!t.push(world(0,1),0));
    CHECK(t.push(world(1,1),100));CHECK(t.sample(100).after);CHECK(!t.push(world(2,2),99));
    CHECK(!t.push(world(1,2),101));CHECK(!t.push(world(2,0),101));CHECK(!t.sample(99).after);
    auto frozen=position(t.sample(500));CHECK(position(t.sample(1000))==frozen);
    CHECK(t.push(world(10,100),2000));CHECK(t.size()==1 && t.stats().resets>0);
    CHECK(position(t.sample(2000))==100);t.reset();CHECK(!t.sample(0).after);
    CHECK(t.push(world(1,1),100));CHECK(t.push(world(2,2),200));
    CHECK(!t.push(world(3,3),150) && !t.sample(150).after);
    for(uint64_t now=200;now<480;now+=20)t.sample(now);
    CHECK(t.push(world(3,23),480));t.sample(480);
    CHECK(t.stats().bufferedMs<=PresentationTimeline::MaximumDelayMs && t.stats().catchups>0);
    for(unsigned hz:{30,60,90,120,144,165,240}){profile(hz,false);profile(hz,true);}
    profile(60,2);profile(120,2);
    std::cout<<"PASS bounded adaptive timeline, no rewind/extrapolation, outage/reset and render-rate profiles\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
