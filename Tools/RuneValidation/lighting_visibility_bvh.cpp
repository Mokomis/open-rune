#include "Light/LightingVisibilityBvh.h"
#include <iostream>
#include <stdexcept>

int main()
{
    std::vector<LightingVisibilityBvh::Triangle> triangles;
    for(int axis=0;axis<3;++axis)for(int side:{-1,1}) {
        dvec3 v[4];int a=(axis+1)%3,b=(axis+2)%3;
        for(int i=0;i<4;++i){v[i][axis]=side;v[i][a]=(i==0||i==3)?-1:1;v[i][b]=i<2?-1:1;}
        dvec3 normal(0);normal[axis]=side;dvec4 plane(normal,-1);
        triangles.push_back({v[0],v[1],v[2],plane});triangles.push_back({v[0],v[2],v[3],plane});
    }
    LightingVisibilityBvh scene(std::move(triangles));
    auto oracle=[](vec3 from,vec3 to){
        dvec3 o(from),d=dvec3(to)-o;double distance=length(d);if(distance<.01)return false;d/=distance;
        for(int axis=0;axis<3;++axis)for(int side:{-1,1}) {
            if(d[axis]==0)continue;
            double t=(side-o[axis])/d[axis];if(t<.01||t>=distance+1)continue;
            auto hit=o+d*t;bool inside=true;
            for(int k=0;k<3;++k)if(k!=axis&&(hit[k]<-1||hit[k]>1))inside=false;
            if(inside)return true;
        }
        return false;
    };
    uint32_t random=55;auto coordinate=[&]{random=random*1664525u+1013904223u;return (int(random%100000)-50000)*.0001f;};
    for(int i=0;i<50000;++i){vec3 a(coordinate(),coordinate(),coordinate()),b(coordinate(),coordinate(),coordinate());if(scene.Trace(a,b)!=oracle(a,b))throw std::runtime_error("BVH differs from analytic cube");}
    if(scene.Trace(vec3(0),vec3(0))||scene.Trace(vec3(2),vec3(2.001f)))throw std::runtime_error("Short ray guard");
    std::vector<LightingVisibilityBvh::Triangle> outside={{dvec3(1,-1,-1),dvec3(1,1,-1),dvec3(1,0,1),dvec4(1,0,0,-1),0}};
    LightingVisibilityBvh clipped(std::move(outside),{{dvec4(1,0,0,-10),-1,true}});
    if(clipped.Trace(vec3(0),vec3(2,0,0))||!clipped.Trace(vec3(11,0,0),vec3(0)))throw std::runtime_error("BSP ancestry predicate changed");
    std::cout<<"PASS: 50000 rays match independent cube-plane oracle; short and degenerate rays rejected\n";
}
