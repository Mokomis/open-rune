#include "../../SurrealEngine/RuneView.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    constexpr float rad=0.01745329251994329577f;
    auto tangent=[&](float degrees){return std::tan(degrees*rad*.5f);};
    auto close=[](float a,float b){assert(std::abs(a-b)<0.0001f);};
    const float aspects[]={4.f/3,3.f/2,16.f/9,21.f/9,32.f/9};
    for(int selection:{70,90,120}) {
        float previous=0;
        for(float aspect:aspects) {
            float fov=RuneView::Adjust(90,90,aspect,selection,true,false);
            assert(fov>previous);previous=fov;
            close(tangent(fov)/aspect,tangent(float(selection))/(4.f/3));
            float zoom=RuneView::Adjust(30,90,aspect,selection,true,false);
            close(tangent(zoom)/tangent(fov),tangent(30)/tangent(90));
            close(RuneView::Adjust(57,90,aspect,selection,true,true),57);
            close(RuneView::Adjust(90,90,aspect,selection,false,false),float(selection));
        }
    }
    close(RuneView::Adjust(90,90,4.f/3,90,true,false),90);
    close(RuneView::Adjust(75,75,4.f/3,90,true,false),90);
    close(tangent(RuneView::Adjust(30,75,4.f/3,90,true,false))/tangent(90),tangent(30)/tangent(75));
    close(RuneView::Adjust(90,90,4.f/3,999,true,false),120);
    close(RuneView::Adjust(90,90,4.f/3,-1,true,false),70);
    assert(std::isfinite(RuneView::Adjust(std::numeric_limits<float>::quiet_NaN(),0,0,90,true,false)));
    std::cout<<"PASS: 4:3/3:2/16:9/21:9/32:9 vertical framing, FOV range, zoom magnification, cinematic bypass, legacy framing and invalid inputs\n";
}
