#pragma once
#include <algorithm>
#include <cmath>

class UActor;
class UPlayerPawn;
namespace RuneStairPresentation {
// Offline Rune's camera already interpolates its anchor. Only follow that
// existing anchor after a small grounded step, never on flat ground merely
// because the camera encountered a ceiling. No simulation coordinates change.
class Height {
    bool ready=false,following=false;
    float previousX=0,previousY=0,previousZ=0,offset=0;
public:
    void reset(){ready=false;following=false;offset=0;}
    float update(float x,float y,float z,float anchorOffset,float dt,float maxStep,bool eligible) {
        if(!eligible || !std::isfinite(x+y+z+anchorOffset+dt+maxStep)) {reset();return 0;}
        if(dt==0 && ready && x==previousX && y==previousY && z==previousZ)return offset;
        float dx=x-previousX,dy=y-previousY,dz=z-previousZ;
        if(!ready || dt<=0 || dt>.1f || dx*dx+dy*dy+dz*dz>64*64 ||
            std::abs(dz)>maxStep+.01f || std::abs(anchorOffset)>48.f) {
            following=false;offset=0;
        }else {
            if(dx*dx+dy*dy>.0001f && std::abs(dz)>.01f)following=true;
            offset=following?anchorOffset:0;
            if(std::abs(offset)<.01f){offset=0;following=false;}
        }
        previousX=x;previousY=y;previousZ=z;ready=true;return offset;
    }
};
void Reset();
void Update(UPlayerPawn*,float dt);
float Offset(UActor*); // Shared by rendered mesh and its culling bounds only.
}
