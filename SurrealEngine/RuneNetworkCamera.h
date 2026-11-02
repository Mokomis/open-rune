#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Presentation only: collision/prediction and the server position are unchanged.
// Frame-rate independent eye-height compensation for small grounded stair steps.
class RuneNetworkCameraHeight {
    bool ready=false;
    uint32_t life=0;
    float z=0,previous=0;
public:
    float update(float target,uint32_t currentLife,bool grounded,float dt,float response=.08f) {
        if(!ready || life!=currentLife || !grounded || dt<=0 || dt>.2f || std::abs(target-previous)>64)z=target;
        else z+= (target-z)*(-std::expm1(-dt/response));
        z=std::clamp(z,target-48.f,target+48.f);
        ready=true;life=currentLife;previous=target;return z;
    }
    void reset(){ready=false;}
};
