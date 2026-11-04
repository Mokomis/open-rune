#pragma once
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
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
        auto bounds=brush->Brush()->BoundingBox.transform(GetMovingBrushTransform(brush));
        // Optional real-pawn rider: stand the player on the top face, let it
        // land and base itself, then require it to ride the whole leg.
        auto* player=commandline->HasArg("","--probe-lift-rider")?game.viewport->Actor():nullptr;
        if(player && frame==20) {
            vec3 top=bounds.center();top.z=bounds.max.z+player->CollisionHeight()+8;
            player->SetLocation(top);player->Velocity()=vec3(0);player->SetPhysics(PHYS_Falling);
        }
        if(player && frame==59) {
            riderStart=player->Location();liftStart=brush->Location();
            LogMessage("RUNE_LIFT_RIDER_READY base="+(player->ActorBase()?player->ActorBase()->Name.ToString():std::string("None"))+
                " physics="+std::to_string(int(player->Physics())));
            if(player->ActorBase()!=brush)throw std::runtime_error("Player did not land on the lift");
        }
        if(frame==60) {
            int target=brush->GetByte("KeyNum")==1?0:1;
            CallEvent(brush,"InterpolateTo",{ExpressionValue::IntValue(target),ExpressionValue::FloatValue(4.f)});
        }
        auto location=brush->Location();
        if(frame>60&&location!=previous)++movingFrames;previous=location;
        vec3 target=bounds.center();target.z=bounds.max.z+4;
        // Stay inside narrow shafts instead of following from behind a wall.
        game.CameraLocation=target+vec3(0,0,96);
        game.CameraRotation=Rotator::FromVector(target-game.CameraLocation);
        if(frame%60==0)LogMessage("RUNE_LIFT_FRAME frame="+std::to_string(frame)+" x="+std::to_string(location.x)+" y="+std::to_string(location.y)+" z="+std::to_string(location.z)+" moving="+std::to_string(movingFrames));
        if(frame==commandline->GetArgInt("","--maxframes",0)-1) {
            if(movingFrames<60)throw std::runtime_error("Lift fixture did not remain in motion");
            if(player) {
                float lift=brush->Location().z-liftStart.z,rider=player->Location().z-riderStart.z;
                LogMessage("RUNE_LIFT_RIDER lift_dz="+std::to_string(lift)+" rider_dz="+std::to_string(rider)+
                    " base="+(player->ActorBase()?player->ActorBase()->Name.ToString():std::string("None"))+" interpolating="+std::to_string((bool)brush->bInterpolating()));
                if(std::abs(lift)<32 || std::abs(rider-lift)>4 || player->ActorBase()!=brush)
                    throw std::runtime_error("Player did not ride the lift");
                LogMessage("RUNE_LIFT_RIDER_OK");
            }
            LogMessage("RUNE_LIFT_FIXTURE_OK moving_frames="+std::to_string(movingFrames));
        }
    }
private:
    UBrush* brush=nullptr;
    vec3 previous{},riderStart{},liftStart{};
    int movingFrames=0;
};
