#include "RuneSnow.h"
#include <cassert>
#include <iostream>
int main() {
    unsigned checks=0;
    for(unsigned seed=0;seed<10000;seed++) {
        float camera=-32000.f+float(seed%1200)*64.f;
        float t=float(seed%100)*.25f;
        float z=RuneSnow::Height(t,seed,camera);
        assert(std::isfinite(z) && z>=camera-384.01f && z<=camera+384.01f);
        assert(RuneSnow::Unit(seed)>=0 && RuneSnow::Unit(seed)<1);
        // Camera translations select only a periodic copy, never drag flakes.
        float translated=RuneSnow::Height(t,seed,camera+100.f);
        assert(std::abs(RuneSnow::Wrap(translated-z+384,768)-384)<.01f);
        float next=RuneSnow::Height(t+.01f,seed,camera);
        float fall=RuneSnow::Wrap(z-next,768);
        assert(fall>.37f && fall<.81f);
        checks+=4;
    }
    assert(RuneSnow::FlakesPerCell(1)<RuneSnow::FlakesPerCell(2));
    assert(RuneSnow::MaxFlakes(1)<RuneSnow::MaxFlakes(2));
    assert(RuneSnow::CoverageLimit(2)<=.06f);
    // Removed Blizzard values, including old saved overrides, cannot exceed Heavy.
    for(int old : {3,4,99}) {
        assert(RuneSnow::ClampMode(old)==RuneSnow::Heavy);
        assert(RuneSnow::FlakesPerCell(old)==RuneSnow::FlakesPerCell(2));
        assert(RuneSnow::MaxFlakes(old)==RuneSnow::MaxFlakes(2));
        assert(RuneSnow::CoverageLimit(old)==RuneSnow::CoverageLimit(2));
        assert(RuneSnow::Style(old).blizzard==0);
        auto fade=RuneSnow::Style(RuneSnow::Conditions{old,old,.5f});
        assert(fade.count==RuneSnow::FlakesPerCell(2)&&fade.blizzard==0);
        checks+=6;
    }
    assert(RuneSnow::Mode.load()==RuneSnow::Automatic);
    assert(RuneSnow::RemoteAge(900000,100,1000000)==100100); // Map predates local boot.
    assert(RuneSnow::RemoteAge(1000000,100,-1000)==0); // No underflow/future weather.
    assert(RuneSnow::RemoteAge(100,1000,500)==1400);
    unsigned levels[4]={};
    for(uint32_t seed=1;seed<=10000;++seed){
        auto period=RuneSnow::Period(seed);assert(period>=180000&&period<=420000);
        for(uint64_t epoch=0;epoch<100;++epoch){auto c=RuneSnow::At(seed,epoch*period+10000);assert(c.from>=0&&c.from<=2&&c.to>=0&&c.to<=2);++checks;}
        auto initial=RuneSnow::At(seed,0);assert(initial.from==initial.to&&initial.blend==1);++levels[initial.to];
        auto edge=RuneSnow::At(seed,period);auto before=RuneSnow::At(seed,period-1);
        assert(edge.from==before.to&&edge.blend==0);
        auto a=RuneSnow::Style(before),b=RuneSnow::Style(edge);assert(std::abs(a.count-b.count)<.001f);
        auto mid=RuneSnow::At(seed,period+10000);assert(mid.blend>.49f&&mid.blend<.51f);
        assert(RuneSnow::At(seed,period+20000).blend==1);
        // Identical schedule for host and late joiner, independent of local clock.
        auto age=period*1000+12345;RuneSnow::Mode=RuneSnow::Automatic;
        RuneSnow::Observe(seed,age,50);auto host=RuneSnow::Current(60);
        RuneSnow::Observe(seed,age+10000,900);auto late=RuneSnow::Current(900);
        assert(host==late);
        for(int forced=0;forced<=3;++forced){RuneSnow::Mode=forced;auto c=RuneSnow::Current(1e9);assert(c.to==RuneSnow::ClampMode(forced)&&c.from==RuneSnow::ClampMode(forced)&&c.blend==1);}
        RuneSnow::Mode=RuneSnow::Automatic;RuneSnow::BeginMap(200,true);assert(!RuneSnow::timeline.ready&&RuneSnow::Current(300).to==0);
        RuneSnow::Observe(seed,age,300);assert(RuneSnow::Current(300)==RuneSnow::At(seed,age));
        checks+=13;
    }
    assert(levels[0]>3500&&levels[0]<4500&&levels[1]>3000&&levels[1]<4000&&levels[2]>2000&&levels[2]<3000&&levels[3]==0);
    RuneSnow::Mode=RuneSnow::Original;assert(RuneSnow::Current(1e9).to==0);RuneSnow::Mode=RuneSnow::Automatic;
    std::cout<<"WEATHER_WEIGHTS clear="<<levels[0]<<" light="<<levels[1]<<" heavy="<<levels[2]<<" blizzard="<<levels[3]<<"\n";
    std::cout<<"PASS "<<checks+11<<" snow continuity, falling speed, density and default checks\n";
}
