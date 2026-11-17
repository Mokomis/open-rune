#pragma once
#include "Engine.h"
#include "GameWindow.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/UViewport.h"
#include "RenderDevice/RenderDevice.h"
#include "RuneCorpse.h"
#include "RunePhysics.h"
#include "Utils/CommandLine.h"
#include "VM/ScriptCall.h"
#include <fstream>

// Bounded isolated gameplay/render fixture. It never writes a normal save.
class RunePhysicsVisualProbe {
  bool runtime = false;
  Array<TextureColor> previousDeathFrame;
  int deathBirthFrame=-1,deathWidth=0,deathHeight=0;
  unsigned impactWallBefore = 0, impactBreakBefore = 0;
  UPawn *impactEnemy = nullptr;
  UActor *impactProp = nullptr;
  vec3 impactLane{0}, impactDir{1, 0, 0}, impactWall{0};
  int startFrame = 0;
  UActor *body = nullptr;
  UActor *prop = nullptr;
  UPawn *enemy = nullptr;
  vec3 center{0}, direction{1, 0, 0}, camera{0};
  unsigned samples = 0;
  bool launched = false;
  bool touchPickup = false, touchThrowReady = false, touchThrow = false;
  int touchHeldFrame = 0;

public:
  void Start(int frame) {
    runtime = true;
    startFrame = frame;
    body = prop = nullptr;
    enemy = nullptr;
    samples = 0;
    launched = false;
  }
  void Stop() {
    runtime = false;
    body = prop = nullptr;
    enemy = nullptr;
  }
  bool Active() const {
    return runtime || commandline->HasArg("", "--physics-playground");
  }
  void Before(Engine &game, int frame) {
    if (!Active())
      return;
    frame -= startFrame;
    if (!runtime && !commandline->HasArg("", "--save-directory"))
      Exception::Throw("Physics playground requires isolated saves");
    auto *p = game.viewport->Actor();
    if (!p)
      return;
    if (commandline->HasArg("", "--physics-environment")) {
      Environment(game, frame);
      return;
    }
    if (frame == 90) {
      bool found = false;
      for (auto *a : game.Level->Actors)
        if (a && a->IsA("PlayerStart")) {
          auto start = a->Location();
          vec3 floor, normal;
          if (!p->Trace(floor, normal, start - vec3(0, 0, 120), start, false,
                        vec3(12)) ||
              normal.z < .7f)
            continue;
          center = floor + vec3(0, 0, p->CollisionHeight() + 1);
          for (int i = 0; i < 16 && !found; ++i) {
            float angle = i * 3.14159265f / 8;
            direction = {std::cos(angle), std::sin(angle), 0};
            auto at = center + direction * 100.f;
            auto farPoint = center + direction * 420.f;
            vec3 farFloor, farNormal;
            if (!p->FastTrace(farPoint, center) ||
                !p->CheckLocation(farPoint, 24, 46, true).first ||
                !p->Trace(farFloor, farNormal, farPoint - vec3(0, 0, 140),
                          farPoint, false, vec3(12)) ||
                farNormal.z < .7f)
              continue;
            if (!p->FastTrace(at, center) ||
                !p->CheckLocation(at, 24, 46, true).first)
              continue;
            auto side = vec3(-direction.y, direction.x, 0);
            auto eye =
                center - direction * 100.f + side * 180.f + vec3(0, 0, 35);
            vec3 hit, n;
            if (p->Trace(hit, n, eye, at, false, vec3(8)))
              continue;
            camera = eye;
            found = true;
          }
          if (found)
            break;
        }
      if (!found)
        Exception::Throw("No physics playground lane");
      p->SetLocation(center);
      p->SetPhysics(PHYS_Walking);
      p->Velocity() = p->Acceleration() = vec3(0);
      p->SetRotation(Rotator::FromVector(direction));
      p->ViewRotation() = p->Rotation();
      p->Health() = 1000;
      auto *a = p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {},
                         center + direction * 100.f, {});
      enemy = UObject::TryCast<UPawn>(a);
      if (!enemy)
        Exception::Throw("Cannot spawn playground opponent");
      enemy->Enemy() = p;
      enemy->Health() = runtime ? 120 : 1000;
      enemy->GotoState("", "");
      enemy->DisableEvent("Tick");
      enemy->SetPhysics(PHYS_Walking);
      enemy->SetRotation(Rotator::FromVector(-direction));
      auto side = vec3(-direction.y, direction.x, 0);
      prop = p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {},
                      center - direction * 50.f + side * 80.f + vec3(0, 0, 45),
                      {});
      if (!prop)
        prop = p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {},
                        center + vec3(0, 0, 100), {});
      if (prop &&
          !RunePhysics::Prop(prop, direction * 100.f + vec3(0, 0, 160), p))
        Exception::Throw("Cannot activate playground prop");
      auto *spike = p->Spawn(
          game.packages->FindClass("RuneI.HelSpikeSmall"), {}, {},
          center + direction * 200.f + side * 65.f - vec3(0, 0, 25), {});
      if (spike)
        RunePhysics::Hazard(spike, 1);
      if (runtime) {
        enemy->EnableEvent("Tick");
        enemy->SetObject("Enemy", p);
        enemy->GotoState("Fighting", "Begin");
      }
      if (runtime)
        CallEvent(
            p, "ClientMessage",
            {ExpressionValue::StringValue(
                "PHYSICS PLAYGROUND: Use lifts props or bodies. Attack / Throw "
                "launches them. Unblocked kicks have a 50% knockdown chance; shield bashes can knock enemies down.")});
      LogMessage("RUNE_PHYSICS_PLAYGROUND_READY");
    }
    if (commandline->HasArg("", "--physics-repeat") && frame > 1350) {
      p->Health() = 1000;
      if (frame % 960 == 480) {
        if (!enemy || enemy->bDeleteMe() || enemy->Health() <= 0) {
          enemy = UObject::TryCast<UPawn>(
              p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {},
                       center + direction * 100.f, {}));
          if (enemy) {
            enemy->Enemy() = p;
            enemy->Health() = 1000;
          }
        }
        if (enemy) {
          enemy->Health() = 1000;
          RunePhysics::Knockdown(enemy, p, direction * 240.f + vec3(0, 0, 210));
        }
      }
    }
    if (runtime && frame > 120) {
      if (frame % 480 == 0 && (!prop || prop->bDeleteMe())) {
        const char *types[] = {"RuneI.Stool", "RuneI.Keg", "RuneI.Bucket"};
        auto side = vec3(-direction.y, direction.x, 0);
        prop = p->Spawn(
            game.packages->FindClass(types[(frame / 480) % 3]), {}, {},
            center - direction * 50.f + side * 80.f + vec3(0, 0, 30), {});
        if (prop)
          RunePhysics::Prop(prop, vec3(0));
      }
      if (frame % 360 == 0 &&
          (!enemy || enemy->bDeleteMe() || enemy->Health() <= 0)) {
        enemy = UObject::TryCast<UPawn>(
            p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {},
                     center + direction * 140.f, {}));
        if (enemy) {
          enemy->Health() = 120;
          enemy->Enemy() = p;
          enemy->GotoState("Fighting", "Begin");
        }
      }
      return;
    }
    if(frame==110 && commandline->HasArg("","--physics-stagger") && enemy) {
      enemy->GotoState("Fighting","Begin");enemy->RuneBashImmunity=0;
      CallEvent(enemy,"JointDamaged",{ExpressionValue::IntValue(24),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(enemy->Location()+vec3(0,0,25)),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
      LogMessage("RUNE_STAGGER_RENDER active="+std::to_string(enemy->RuneBashPhase==3));
    }
    if(frame==110 && commandline->HasArg("","--physics-muscles") && enemy) {
      auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(enemy));auto bones=m->ActorJointMatrices(enemy);int head=0;
      for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name=="head")head=int(j);
      auto hit=(GetRuneMeshToWorld(enemy)*bones[head]*vec4(0,0,0,1)).xyz();
      bool reacted=RunePhysics::React(enemy,hit,direction*600.f+vec3(0,0,80),head);
      LogMessage("RUNE_PUPPET_RENDER_IMPULSE active="+std::to_string(reacted));
    }
    if (frame == 150) {
      if (!RunePhysics::Controls(enemy) &&
          !RunePhysics::Knockdown(enemy, p,
                                  direction * 230.f + vec3(0, 0, 190)))
        Exception::Throw("Playground knockdown failed");
      launched = true;
    }
    if (frame > 150 && enemy && !enemy->bDeleteMe()) {
      ++samples;
      auto *m = UObject::TryCast<USkelModel>(GetRuneDisplayMesh(enemy));
      for (auto &b : m->ActorJointMatrices(enemy))
        for (int i = 0; i < 16; ++i)
          if (!std::isfinite(b[i]))
            Exception::Throw("Invalid physical skin matrix");
      if (frame == 650 && RunePhysics::Controls(enemy))
        Exception::Throw("Playground opponent did not recover");
    }
    if (frame == 700 && enemy) {
      enemy->Health() = 0;
      CallEvent(enemy, "Died",
                {ExpressionValue::ObjectValue(p),
                 ExpressionValue::NameValue("blunt"),
                 ExpressionValue::VectorValue(enemy->Location())});
    }
    // Keep the test camera on the same body across the original pawn/carcass
    // replacement; falling back to the player would hide handoff defects.
    if(frame>=700 && !body && enemy && enemy->bDeleteMe())
      for(auto* a:game.Level->Actors)
        if(a && !a->bDeleteMe() && a->IsA("RuneCarcass")){body=a;break;}
    if (frame == 950) {
      for (auto *a : game.Level->Actors)
        if (a && !a->bDeleteMe() && a->IsA("RuneCarcass")) {
          body = a;
          break;
        }
      if (!body)
        Exception::Throw("Playground death did not produce corpse");
      if (!RunePhysics::Controls(body))
        RunePhysics::Ragdoll(body, direction * 220.f + vec3(0, 0, 180), p);
    }
    const bool touch = commandline->HasArg("", "--physics-touch");
    if (frame == 1080 && body) {
      auto reach = RuneCorpse::InteractionPoint(body);
      bool approached = false;
      for (int i = 0; i < 32 && !approached; ++i) {
        float angle = i * 3.14159265f / 16;
        vec3 d(std::cos(angle), std::sin(angle), 0);
        auto at = reach - d * 65.f;
        vec3 floor, normal;
        if (!p->Trace(floor, normal, at - vec3(0, 0, 120), at + vec3(0, 0, 80),
                      false, vec3(0)) ||
            normal.z < .7f)
          continue;
        at = floor + vec3(0, 0, p->CollisionHeight() + 1);
        if (p->CheckLocation(at, p->CollisionRadius(), p->CollisionHeight(),
                             true)
                .first &&
            p->SetLocation(at)) {
          p->SetRotation(Rotator::FromVector(d));
          approached = true;
        }
      }
      if (!approached)
        Exception::Throw("Cannot approach physical corpse");
      p->SetPhysics(PHYS_Walking);
      p->Velocity() = p->Acceleration() = vec3(0);
      for (auto *a : game.Level->Actors)
        if (a && !a->bDeleteMe() && a->IsA("Inventory") && !a->Owner() &&
            length(a->Location() - p->Location()) < 140)
          a->bHidden() = true;
      p->GotoState("PlayerWalking", "Begin");
      if (auto *proxy = p->GetUObject("AnimProxy"))
        proxy->GotoState("Idle", "Begin");
      p->CorpseCarry.cooldown = 0;
      if (touch)
        LogMessage("RUNE_PHYSICS_TOUCH_USE_READY");
      else {
        CallEvent(p, "Use");
        if (RuneCorpse::Held(p) != body)
          Exception::Throw("Natural physical corpse Use pickup failed");
      }
    }
    if (touch && frame > 1080 && body) {
      if (!touchPickup && RuneCorpse::Held(p) == body) {
        touchPickup = true;
        touchHeldFrame = frame;
        LogMessage("RUNE_PHYSICS_TOUCH_PICKUP_OK");
      }
      if (touchPickup && !touchThrowReady && frame > touchHeldFrame + 60) {
        touchThrowReady = true;
        LogMessage("RUNE_PHYSICS_TOUCH_THROW_READY");
      }
      if (touchThrowReady && !touchThrow && !RuneCorpse::Held(p) &&
          RunePhysics::Controls(body)) {
        touchThrow = true;
        LogMessage("RUNE_PHYSICS_TOUCH_THROW_OK");
      }
    }
    if (!touch && frame == 1140 && body)
      CallEvent(p, "Throw");
    if (frame >= 90) {
      auto *focus = body && !body->bDeleteMe() ? body
                    : enemy && !enemy->bDeleteMe()
                        ? static_cast<UActor *>(enemy)
                        : static_cast<UActor *>(p);
      vec3 aim = focus->Location();
      if (auto *m = UObject::TryCast<USkelModel>(GetRuneDisplayMesh(focus))) {
        auto bones = m->ActorJointMatrices(focus);
        if (bones.size() > 1)
          aim = (GetRuneMeshToWorld(focus) * bones[1] * vec4(0, 0, 0, 1)).xyz();
      }
      if (RuneCorpse::Held(p))
        aim = p->Location() + vec3(0, 0, 35);
      auto eye = aim - direction * 140.f +
                 vec3(-direction.y, direction.x, 0) * 190.f + vec3(0, 0, 55);
      vec3 hit, normal;
      if (p->Trace(hit, normal, eye, aim, false, vec3(5)))
        eye = hit + normal * 7.f;
      game.CameraLocation = eye;
      game.CameraRotation = Rotator::FromVector(aim - eye);
      p->bBehindView() = true;
      p->SetBool("bClientSideAlpha", false);
      p->SetFloat("ClientSideAlphaScale", 1);
    }
  }
  void Environment(Engine &game, int frame) {
    if (commandline->HasArg("", "--physics-repeat"))
      frame %= 800;
    auto *p = game.viewport->Actor();
    if (!p)
      return;
    const bool impactExpected =
        commandline->GetArgInt("", "--physics-impacts", 1) != 0;
    if (frame == 90) {
      p->bHidden() = true; // Isolated spectator fixture: keep the local model
                           // out of its camera.
      impactWallBefore = RunePhysics::Stats().wallSlams;
      impactBreakBefore = RunePhysics::Stats().brokenProps;
      if (!RunePhysics::FindImpactLane(p, impactLane, impactDir, impactWall))
        Exception::Throw("No authored impact visual lane");
      p->SetLocation(impactLane - impactDir * 110.f);
      p->SetPhysics(PHYS_Walking);
      p->Velocity() = p->Acceleration() = vec3(0);
      p->Health() = 1000;
      p->SetRotation(Rotator::FromVector(impactDir));
      p->ViewRotation() = p->Rotation();
      impactEnemy = UObject::TryCast<UPawn>(
          p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {},
                   impactLane, {}));
      if (!impactEnemy)
        Exception::Throw("No wall-slam opponent");
      impactEnemy->Health() = 1000;
      impactEnemy->Enemy() = p;
      impactEnemy->GotoState("", "");
      impactEnemy->DisableEvent("Tick");
    }
    if (frame == 150) {
      if (!RunePhysics::Knockdown(impactEnemy, p,
                                  impactDir * 650.f + vec3(0, 0, 80)))
        Exception::Throw("Wall slam visual launch failed");
      launched = true;
    }
    if (impactExpected && frame == 400 &&
        RunePhysics::Stats().wallSlams <= impactWallBefore)
      Exception::Throw("No rendered wall slam");
    if (frame == 450) {
      impactProp = p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {},
                            impactLane + vec3(0, 0, -22), {});
      if (!impactProp || !RunePhysics::Prop(impactProp, impactDir * 650.f, p))
        Exception::Throw("Prop smash visual launch failed");
    }
    if (impactExpected && frame == 650 &&
        RunePhysics::Stats().brokenProps <= impactBreakBefore)
      Exception::Throw("No rendered prop break");
    if (frame == 400 && impactEnemy) {
      impactEnemy->Enemy() = nullptr;
      if (!RunePhysics::Controls(impactEnemy))
        impactEnemy->GotoState("", "");
    }
    if (frame == 750) {
      LogMessage("RUNE_PHYSICS_ENVIRONMENT_CYCLE walls=" +
                 std::to_string(RunePhysics::Stats().wallSlams) +
                 " breaks=" + std::to_string(RunePhysics::Stats().brokenProps));
      if (impactEnemy && !impactEnemy->bDeleteMe())
        impactEnemy->Destroy();
      if (impactProp && !impactProp->bDeleteMe()) {
        if (!impactExpected)
          impactProp->SetBool("bDestroyable", false);
        impactProp->Destroy();
      }
      impactEnemy = nullptr;
      impactProp = nullptr;
    }
    if (frame >= 90) {
      ++samples;
      vec3 aim = impactLane + impactDir * 25.f + vec3(0, 0, -20);
      auto side = vec3(-impactDir.y, impactDir.x, 0);
      auto eye = aim - impactDir * 160.f + side * 80.f + vec3(0, 0, 55);
      vec3 hit, n;
      if (p->Trace(hit, n, eye, aim, false, vec3(6)))
        eye = hit + n * 10.f;
      if (!p->CheckLocation(eye, 4, 4, false).first || !p->FastTrace(eye, aim))
        eye = p->Location() + vec3(0, 0, 20);
      game.CameraLocation = eye;
      game.CameraRotation = Rotator::FromVector(aim - eye);
      p->bBehindView() = true;
      p->SetBool("bClientSideAlpha", false);
      p->SetFloat("ClientSideAlphaScale", 1);
    }
  }
  void After(Engine &game, int frame) {
    if (!Active())
      return;
    frame -= startFrame;
    auto deathPrefix=commandline->GetArg("","--physics-death-captures","");
    if(!deathPrefix.empty() && frame>=700 && frame<=1000) {
      bool born=false;
      for(auto* a:game.Level->Actors)
        if(a && !a->bDeleteMe() && a->IsA("RuneCarcass") && RunePhysics::Controls(a))born=true;
      auto* device=game.window->GetRenderDevice();
      auto write=[&](const Array<TextureColor>& pixels,const std::string& suffix) {
        std::ofstream out(deathPrefix+suffix+".ppm",std::ios::binary);
        out<<"P6\n"<<deathWidth<<' '<<deathHeight<<"\n255\n";
        for(auto c:pixels){out.put(char(c.R));out.put(char(c.G));out.put(char(c.B));}
      };
      if(deathBirthFrame<0 || frame==deathBirthFrame+30) {
        deathWidth=device->GetRenderWidth();deathHeight=device->GetRenderHeight();
        Array<TextureColor> pixels;pixels.resize(size_t(deathWidth)*deathHeight);
        bool gamma=device->GammaCorrectScreenshots;device->GammaCorrectScreenshots=true;
        device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=gamma;
        if(born && deathBirthFrame<0) {
          if(!previousDeathFrame.empty())write(previousDeathFrame,"-before");
          write(pixels,"-handoff");deathBirthFrame=frame;
          LogMessage("RUNE_PHYSICS_DEATH_RENDER frame="+std::to_string(frame));
        } else if(frame==deathBirthFrame+30)write(pixels,"-rested");
        if(deathBirthFrame<0)previousDeathFrame=std::move(pixels);
        else previousDeathFrame.clear();
      }
    }
    if (frame == 90 || frame == 110 || frame == 130 || frame == 175 || frame == 210 || frame == 270 ||
        frame == 400 || frame == 475 || frame == 500 || frame == 650 ||
        frame == 1000 || frame == 1120 || frame == 1190 || frame == 1300) {
      auto prefix = commandline->GetArg("", "--physics-captures", "");
      if (prefix.empty())
        return;
      auto *d = game.window->GetRenderDevice();
      int w = d->GetRenderWidth(), h = d->GetRenderHeight();
      Array<TextureColor> pixels;
      pixels.resize(size_t(w) * h);
      bool gamma = d->GammaCorrectScreenshots;
      d->GammaCorrectScreenshots = true;
      d->ReadPixels(pixels.data());
      d->GammaCorrectScreenshots = gamma;
      std::ofstream f(prefix + "-" + std::to_string(frame) + ".ppm",
                      std::ios::binary);
      f << "P6\n" << w << ' ' << h << "\n255\n";
      for (auto c : pixels) {
        const char rgb[] = {char(c.R), char(c.G), char(c.B)};
        f.write(rgb, 3);
      }
      LogMessage("RUNE_PHYSICS_RENDER frame=" + std::to_string(frame) + " " +
                 RunePhysics::Status());
    }
  }
  void Finish() {
    if (commandline->HasArg("", "--physics-playground")) {
      if (!launched || samples < 100)
        Exception::Throw("Physics render fixture incomplete");
      if (commandline->HasArg("", "--physics-touch") && !touchThrow)
        Exception::Throw("Actual physical corpse touch sequence incomplete");
      LogMessage("RUNE_PHYSICS_RENDER_OK samples=" + std::to_string(samples));
    }
  }
};
