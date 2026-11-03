#pragma once
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"

// Bounded rendered fixture: invoke an original vertical brush interpolation and
// follow its current top face. Does not save, change assets or affect normal play.
class RuneLiftProbe
{
public:
    void Tick(Engine& game,int frame)
    {
        if(!commandline->HasArg("","--probe-lift"))return;
        if(!commandline->HasArg("","--save-directory")||commandline->GetArgInt("","--maxframes",0)<180)
            throw std::runtime_error("Lift fixture requires bounded frames and isolated saves");
        if(!brush) {
            auto name=commandline->GetArg("","--probe-lift","auto");float best=-1;
            for(auto* actor:game.Level->Actors)if(auto* candidate=GetMovingBrush(actor);candidate&&candidate->Brush()) {
                if(name!="auto"&&candidate->Name!=name)continue;
                float travel=std::abs(MovingBrushKeyPos(candidate,1).z-MovingBrushKeyPos(candidate,0).z);
                if(travel>best&&travel>32){best=travel;brush=candidate;}
            }
            if(!brush)throw std::runtime_error("No vertical lift found for fixture");
            brush->bStasis()=false;previous=brush->Location();
            LogMessage("RUNE_LIFT_FIXTURE actor="+brush->Name.ToString()+" travel_z="+std::to_string(best));
        }
        if(frame==60) {
            int target=brush->GetByte("KeyNum")==1?0:1;
            CallEvent(brush,"InterpolateTo",{ExpressionValue::IntValue(target),ExpressionValue::FloatValue(4.f)});
        }
        auto location=brush->Location();
        if(frame>60&&location!=previous)++movingFrames;previous=location;
        auto bounds=brush->Brush()->BoundingBox.transform(GetMovingBrushTransform(brush));
        vec3 target=bounds.center();target.z=bounds.max.z+4;
        // Stay inside narrow shafts instead of following from behind a wall.
        game.CameraLocation=target+vec3(0,0,96);
        game.CameraRotation=Rotator::FromVector(target-game.CameraLocation);
        if(frame%60==0)LogMessage("RUNE_LIFT_FRAME frame="+std::to_string(frame)+" x="+std::to_string(location.x)+" y="+std::to_string(location.y)+" z="+std::to_string(location.z)+" moving="+std::to_string(movingFrames));
        if(frame==commandline->GetArgInt("","--maxframes",0)-1) {
            if(movingFrames<60)throw std::runtime_error("Lift fixture did not remain in motion");
            LogMessage("RUNE_LIFT_FIXTURE_OK moving_frames="+std::to_string(movingFrames));
        }
    }
private:
    UBrush* brush=nullptr;
    vec3 previous{};
    int movingFrames=0;
};
