#include "SurrealEngine/RenderDevice/Vulkan/MaterialMaps.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace MaterialMaps;
    assert(Classify("IRON_SWORD")==Metal && Classify("RagnarSkin")==Skin && Classify("Brckston")==Stone);
    assert(Classify("VikingShips.VSailtest_01")==Cloth && Classify("VikingShips.Hull")==Wood);
    std::vector<float> flat(64,.5f),warm(64,.2f);
    auto metal=Generate(flat,warm,8,8,Metal),stone=Generate(flat,warm,8,8,Stone);
    assert(metal[3]<stone[3]-80);
    for(int i=0;i<64;i++)assert(stone[i*4]==128 && stone[i*4+1]==128 && stone[i*4+2]==255);
    std::vector<float> ridges(256),neutral(256,0);
    for(int y=0;y<16;y++)for(int x=0;x<16;x++)ridges[y*16+x]=.5f+.3f*std::sin(x*.8f);
    auto detail=Generate(ridges,neutral,16,16,Stone);int changed=0;
    for(int i=0;i<256;i++)changed+=std::abs(int(detail[i*4])-128)>10;
    assert(changed>100);
    for(int size=16;size>1;size/=2) {
        detail=Downsample(detail,size,size);
        for(size_t i=0;i<detail.size();i+=4) {
            float length=0;for(int j=0;j<3;j++){float n=detail[i+j]/127.5f-1;length+=n*n;}
            assert(std::abs(length-1)<.025f);assert(detail[i+3]>=46);
        }
    }
    std::cout<<"RUNE_MATERIAL_MAPS_CPU_OK profiles=1 flat=1 normals=1 roughness=1 mip_normalization=1\n";
}
