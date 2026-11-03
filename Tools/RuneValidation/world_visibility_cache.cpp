#include "Light/WorldVisibilityCache.h"
#include <iostream>
#include <stdexcept>

int main()
{
    auto require=[](bool ok){if(!ok)throw std::runtime_error("World visibility cache regression");};
    WorldVisibilityCache cache;
    int worldQueries=0,moverQueries=0;
    bool door=false;
    auto test=[&](vec3 from,vec3 to) {
        cache.Begin();
        bool result=cache.Trace(from,to,
            [&] { ++worldQueries;return from.x<5&&to.x>5; },
            [&] { ++moverQueries;return door; });
        cache.End();return result;
    };
    require(!test(vec3(0),vec3(1)));
    for(int frame=0;frame<100;++frame) {door=(frame%2)==0;require(test(vec3(0),vec3(1))==door);}
    require(worldQueries==1&&moverQueries==101);
    door=false;require(test(vec3(0),vec3(8)));require(worldQueries==2&&moverQueries==101);
    require(!test(vec3(6,0,0),vec3(8)));require(worldQueries==3);
    require(!test(vec3(6.001f,0,0),vec3(8)));require(worldQueries==4);
    // A map change destroys the old cache; static-world results cannot survive it.
    cache=WorldVisibilityCache{};test(vec3(6.001f,0,0),vec3(8));require(worldQueries==5);
    cache.Begin();cache.End();test(vec3(6.001f,0,0),vec3(8));require(worldQueries==6);
    std::cout<<"PASS: unchanged rays memoized; moving occluders rechecked; endpoint, map and list changes invalidate\n";
}
