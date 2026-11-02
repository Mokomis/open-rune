#pragma once
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/USound.h"

// Filter only the original bloodlust cue, preserving combat voices, music and
// unrelated actor ambience. Applied to one-shots and already-playing loops.
inline bool IsRuneBloodlustSound(UActor* actor,USound* sound) {
    if(!actor || !sound || !actor->IsA("RunePlayer"))return false;
    for(auto name:{"BerserkSoundStart","BerserkSoundLoop","BerserkSoundEnd"})
        if(actor->GetUObject(name)==sound)return true;
    return false;
}
