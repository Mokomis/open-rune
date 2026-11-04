#include "GorePhysics.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace GorePhysics;
    auto floor=[](vec3 from,vec3 to,float radius) {
        if(to.z>=radius)return Hit{};
        float t=(from.z-radius)/std::max(from.z-to.z,.0001f);
        return Hit{true,std::clamp(t,0.f,1.f),vec3(0,0,1),true};
    };
    vec3 reference(0);
    for(int hz:{30,60,120}) {
        Body wall;wall.live=true;wall.velocity=vec3(120,0,0);
    Step(wall,1.f/120,vec3(0),[](vec3 from,vec3 to,float) {
        if(to.x<=.5f)return Hit{};
        return Hit{true,(.5f-from.x)/(to.x-from.x),vec3(-1,0,0),true};
    });
    assert(wall.velocity.x<0 && wall.position.x>.2f && wall.position.x<.5f && wall.contacts==1);
    Body b;b.live=true;b.position=vec3(0,0,100);b.velocity=vec3(120,35,150);b.spin=vec3(2,3,4);
        int impacts=0;
        for(int i=0;i<hz*3;++i)if(Step(b,1.f/hz,vec3(0,0,-950),floor).hit)++impacts;
        assert(b.live&&b.sleeping&&b.contacts>0&&b.contacts<=8&&impacts>0);
        assert(b.position.z>=b.radius && b.position.z<b.radius+.2f);
        assert(length(b.velocity)==0 && length(b.spin)==0);
        if(hz==30)reference=b.position;else assert(length(reference-b.position)<.001f);
        auto resting=b.position;Step(b,.5f,vec3(0,0,-950),floor);assert(b.position==resting);
        for(int i=0;i<hz*5;++i)Step(b,1.f/hz,vec3(0,0,-950),floor);
        assert(!b.live);
    }
    Body b;b.live=true;b.position=vec3(0,0,40);b.velocity=vec3(100,0,0);
    Step(b,1.f/120,vec3(0),[](vec3,vec3,float){return Hit{true,.25f,vec3(-1,0,0),false};});
    assert(!b.live); // moving surfaces retire cosmetic fragments
    b={};b.live=true;b.position=vec3(0,0,40);
    Step(b,0,vec3(0,0,-950),floor);assert(b.position==vec3(0,0,40));
    auto u=Tangent(normalize(vec3(.3f,.4f,.8f)));
    assert(std::abs(dot(u,normalize(vec3(.3f,.4f,.8f))))<1e-5f);
    std::cout<<"GORE_PHYSICS_OK rates=30,60,120 sleep=1 bounded_bounce=1 remaining_time=1 moving_surface=1 pause=1 tangent=1\n";
}
