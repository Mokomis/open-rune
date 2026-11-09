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

// A conservative spring arm: move in before upcoming stair treads, ease back
// out after an obstruction, and always obey the current collision-safe length.
class RuneNetworkCameraBoom {
    bool ready=false;uint32_t life=0;float length=185;
public:
    float update(float clear,float preview,uint32_t currentLife,bool continuous,float dt){
        clear=std::clamp(clear,0.f,185.f);preview=std::clamp(preview,0.f,clear);
        if(!ready||life!=currentLife||!continuous||dt<=0||dt>.2f)length=clear;
        else length+=(preview-length)*(-std::expm1(-dt/.14f));
        length=std::clamp(length,0.f,clear);ready=true;life=currentLife;return length;
    }
    void reset(){ready=false;}
};
