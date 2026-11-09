#include "Precomp.h"
#include "Render/BspClipper.h"
#include "Math/bbox.h"
#include <iostream>
#include <random>
int main() {
    BspClipper clipper;std::mt19937 rng(782);
    auto value=[&](){return float(rng()%200000)/100000.f-1;};
    unsigned visible=0,missing=0;
    for(int i=0;i<20000;++i) {
        clipper.Setup(mat4::identity(),{},vec4(0));
        // A random triangle is contained in this exact flat AABB. If the
        // triangle survives the clipper, its enclosing box must survive too.
        vec3 v[3];for(auto& p:v)p={value(),value(),.5f};
        BBox box(v[0],v[0]);
        for(auto p:v)for(int k=0;k<3;++k){box.min[k]=std::min(box.min[k],p[k]);box.max[k]=std::max(box.max[k],p[k]);}
        if(clipper.CheckSurface(v,3,false)) {
            ++visible;if(!clipper.IsAABBVisible(box)){++missing;if(missing<3)std::cout<<"MISSING case="<<i<<std::endl;}
        }
    }
    std::cout<<"BSP_BOUNDS visible="<<visible<<" missing="<<missing<<std::endl;
    return missing?1:0;
}
