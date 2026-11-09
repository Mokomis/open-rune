#pragma once
#include "VisibleRuneEffects.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"

namespace RuneBashPresentation {
inline void Draw(VisibleFrame* frame) {
    if(frame->ShadowView || frame->PortalDepth || frame->MirrorFlag || !engine->LaunchInfo.IsRune() || !RuneVisualEffects::Enabled.load())return;
    int budget=128; // Shared with other particles; no lights, actors or physics queries.
    auto texture=RuneEffects::Texture(0,false);
    for(auto* actor:engine->Level->Actors) {
        auto* p=UObject::TryCast<UPawn>(actor);
        if(!p || p->bDeleteMe() || p->bHidden() || p->Health()<=0 || (p->AnimSequence()!="H3_DefendAttack" && p->Bloodlust.phase!=RuneBloodlust::Charging))continue;
        const bool rushing=p->Bloodlust.phase==RuneBloodlust::Charging;
        float phase=rushing?std::fmod(p->Bloodlust.age*3.5f,1.f):(p->AnimFrame()-.02f)/.40f;
        if((!rushing && (phase<=0 || phase>=1)) || length(p->Location()-frame->ViewLocation.xyz())>1200)continue;
        auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));
        if(!shield || shield->bDeleteMe() || shield->bHidden() || shield->Owner()!=p || shield->GetUObject("AttachParent")!=p)continue;
        auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
        int joint=shield->GetByte("AttachParentJoint");
        if(!model || joint>=int(model->Joints.size()))continue;
        auto bones=model->ActorJointMatrices(p);
        auto point=(GetRuneMeshToWorld(p)*bones[joint]*vec4(0,0,0,1)).xyz();
        auto axes=Coords::Rotation(p->Rotation());
        auto forward=axes.XAxis,right=axes.YAxis,up=vec3(0,0,1);
        point+=forward*(6+phase*12);
        float radius=(17+phase*12)*std::clamp(p->DrawScale(),.5f,2.f);
        float opacity=rushing?.7f:.55f*std::sin(phase*3.14159265f);
        if(rushing){
            // Continuous shield-edge slipstreams, shared by authority and
            // replica pawns. Cosmetic quads only; no particles or lights spawn.
            for(int streak=0;streak<4;++streak){
                if(budget--<=0 || frame->EffectQuadBudget<=0)return;
                float a=streak*1.57079633f+phase*.25f;
                vec3 radial=right*std::cos(a)+up*std::sin(a);
                float tail=35.f+phase*28.f;
                RuneEffects::Quad(frame,texture,point+radial*radius-forward*tail,
                    forward*tail,radial*1.8f,vec3(.73f,.85f,1.f),.55f,PF_Highlighted);
            }
        }
        for(int arc=0;arc<2;++arc)for(int segment=0;segment<8;++segment) {
            if(budget--<=0 || frame->EffectQuadBudget<=0)return;
            float a=arc*3.14159265f+phase*.9f+(segment-3.5f)*.21f;
            vec3 radial=right*std::cos(a)+up*std::sin(a);
            vec3 tangent=-right*std::sin(a)+up*std::cos(a);
            float taper=std::sin((segment+.5f)*3.14159265f/8);
            RuneEffects::Quad(frame,texture,point+radial*radius,tangent*(radius*.23f),radial*(1.3f*taper),vec3(.73f,.85f,1.f),opacity*taper,PF_Highlighted);
        }
    }
}
}
