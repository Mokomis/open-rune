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
    assert(RuneSnow::Mode.load()==0);
    std::cout<<"PASS "<<checks+4<<" snow continuity, falling speed, density and default checks\n";
}
