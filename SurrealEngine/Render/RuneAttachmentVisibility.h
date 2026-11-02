#pragma once
#include "Packages/Engine/Actors/UActor.h"

struct RuneAttachmentVisibility
{
    bool HiddenParent = false;
    bool AttachedToPlayer = false;
};

inline RuneAttachmentVisibility GetRuneAttachmentVisibility(UActor* actor, UActor* player)
{
    RuneAttachmentVisibility result;
    for (int depth = 0; actor && actor->bCarriedItem(); depth++)
    {
        if (depth == 16) { result.HiddenParent = true; break; }
        actor = UObject::TryCast<UActor>(actor->GetUObject("AttachParent"));
        if (!actor) break;
        result.HiddenParent |= actor->bHidden();
        result.AttachedToPlayer |= actor == player;
    }
    return result;
}
