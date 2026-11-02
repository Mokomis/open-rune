// Compare the production span clipper against an independent pixel-coverage oracle.
#include "Precomp.h"
#define private public
#include "Render/BspClipper.h"
#undef private
#include <array>
#include <random>
#include <iostream>

int main()
{
    std::mt19937 random(0x52554e45);
    BspClipper clipper;
    size_t checks=0;
    for(int trial=0;trial<500;++trial)
    {
        clipper.Setup(mat4::identity(),{},vec4(0));
        clipper.numDrawSpans=0;
        std::array<std::array<bool,2048>,8> covered={};
        for(int operation=0;operation<200;++operation)
        {
            int y=random()%8, left=random()%2049;
            int right=std::min(2048,left+(int)(random()%320));
            bool solid=random()%4!=0, collect=random()%3==0;
            bool expected=false;
            std::vector<std::pair<int,int>> gaps;
            for(int x=left;x<right;)
            {
                if(covered[y][x]) { ++x; continue; }
                int start=x;
                while(x<right && !covered[y][x]) ++x;
                gaps.emplace_back(start,x);
                expected=true;
            }
            clipper.CollectSpans=collect;
            clipper.VisibleSpans.clear();
            bool actual=clipper.DrawSpan(y,left,right,solid);
            if(actual!=expected) { std::cerr<<"visibility mismatch "<<trial<<":"<<operation<<"\n"; return 1; }
            if(solid && collect)
            {
                if(clipper.VisibleSpans.size()!=gaps.size()) { std::cerr<<"portal gap count mismatch "<<trial<<":"<<operation<<"\n"; return 1; }
                for(size_t i=0;i<gaps.size();++i)
                    if(clipper.VisibleSpans[i].x0!=gaps[i].first || clipper.VisibleSpans[i].x1!=gaps[i].second || clipper.VisibleSpans[i].y!=y) { std::cerr<<"portal gap mismatch\n";return 1; }
            }
            if(solid) for(int x=left;x<right;++x) covered[y][x]=true;
            std::array<bool,2048> spans={};
            for(const auto& span:clipper.Viewport[y])
                for(int x=std::max(0,(int)span.x0);x<std::min(2048,(int)span.x1);++x) spans[x]=true;
            if(spans!=covered[y]) { std::cerr<<"coverage mismatch "<<trial<<":"<<operation<<"\n"; return 1; }
            ++checks;
        }
    }
    std::cout<<"BSP_SPAN_ORACLE_OK operations="<<checks<<"\n";
}
