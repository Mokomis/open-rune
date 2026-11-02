#pragma once
#include "Packages/Engine/Actors/UActor.h"

// Rune separates the runtime sprite from Texture, which may hold an editor icon.
// A null Sprite is intentional (for example an active LookTarget).
inline UTexture* GetActorSpriteTexture(UActor* actor, bool isRune)
{
    return isRune ? actor->Sprite() : actor->Texture();
}
