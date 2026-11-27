#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RunePhysics.h"
#include "RuneIK.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/UViewport.h"
#include "Physics/RunePhysicsWorld.h"
#include "RuneBloodlust.h"
#include "RuneCorpse.h"
#include "RuneDualWield.h"
#include "RuneFeedback.h"
#include "RuneKick.h"
#include "RuneSlam.h"
#include "RuneCharacters.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Utils/Random.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <limits>
#include <set>
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif

void RunRuneCeilingProbe(Engine&,UPlayerPawn*);
namespace RunePhysics {
namespace {
using namespace RuneRigid;
struct Part {
  Handle body = Invalid;
  int joint = 0, parent = -1;
  Rotation bindBody, bindJoint;
  vec3 bindCenter{0}, bindPoint{0};
  RuneRigid::Transform current,lastValid;
};
struct Saved {
  uint8_t physics = 0;
  bool collide = false, blockActors = false, blockPlayers = false,
       world = false;
  float radius = 0, height = 0;
};
struct DeathSeed {
  USkelModel *model = nullptr;
  Array<RuneJointPose> pose;
  mat4 world = mat4::identity();
  vec3 location{0}, prePivot{0}, velocity{0};
  Rotator rotation{0, 0, 0};
  bool resting = false;
};
struct DeathObservation {
  DeathSeed seed;
  float initialError = 0;
  bool slept = false;
};
std::map<UActor *, DeathSeed> pendingDeaths;
std::map<UActor *, DeathObservation>
    deathObservations; // Only populated by isolated death probe.
struct State {
  UActor *actor = nullptr;
  USkelModel *model = nullptr;
  UPawn *instigator = nullptr;
  Saved saved;
  std::vector<Part> parts;
  Array<RuneJointPose> rest, pose, previousPose;
  vec3 previousLocation{0};
  RuneRigid::Transform previousBody, currentBody, recoveryBody;
  mat4 bindWorld = mat4::identity();
  vec3 bindLocation{0}, actorOrigin{0};
  Rotation bindActor;
  bool living = false, prop = false, replica = false, recovering = false, driven = false;
  std::set<int> activeJoints;
  Array<RuneJointPose> targetPose;
  vec3 puppetOrigin{0};
  Rotator puppetRotation{0,0,0};
  bool puppetPresented=false;
#ifdef OPENRUNE_ENABLE_NETWORK
  std::vector<openrune::net::PhysicalBasis> basis;
#endif
  mat4 replicaWorld = mat4::identity();
  float age = 0, recovery = 0, still = 0;
  double lastImpact = -100;
  bool impactArmed = false, wallSlamDone = false;
  float stunUntil = 0;
  UPawn *impaler = nullptr;
  UActor *pinWeapon = nullptr, *pinOffhand = nullptr;
  int pinnedPart = -1;
  vec3 pinOrigin{0}, pinAim{0}, pinLastHolder{0};
  float pullback=0, sidestep=0;
  Transform pinTransform{};
  float pinYaw = 0, pinLifeSpan = 0, pinTimerRate = 0;
  UPawn *carrier = nullptr;
  UActor *originalOwner = nullptr;
  float release = -1, carryHeight = 16;
  vec3 recoveryLocation{0}, recoveryStart{0};
  std::set<UActor *> victims;
};
std::unique_ptr<World> world;
ULevel *currentLevel = nullptr;
int applied = 0;
float accumulator = 0, scan = 0;
double simulationTime = 0;
std::map<UActor *, State> states;
std::map<UActor *, State> puppets;
void StepPuppets(float dt,bool before);
void DriveRecovery(State&,float);
void clearPuppet(UActor* a){auto it=puppets.find(a);if(it==puppets.end())return;if(world)for(auto& p:it->second.parts)world->remove(p.body);puppets.erase(it);}
bool puppetEnabled(){return commandline->GetArgInt("","--rune-puppet",1)!=0;}
std::map<UPawn *, double> resistantUntil;
std::set<UActor *> retiredBodies;
std::map<UActor *, Handle> proxies, movers;
struct HazardState {
  int kind = 1;
  std::map<UActor *, double> cooldown;
};
std::map<UActor *, HazardState> hazards;
Counters counters;
unsigned wallSlams = 0, brokenProps = 0;
double nextImpactEffect = 0;
uint32_t nextGroup = 1;
unsigned currentEvaluation = 0, presentationDepth = 0;
struct CurrentScope {
  CurrentScope() { ++currentEvaluation; }
  ~CurrentScope() { --currentEvaluation; }
};
float renderAlpha() {
  return (currentEvaluation || !presentationDepth)
             ? 1.f
             : std::clamp(accumulator * 60.f, 0.f, 1.f);
}

bool authority(UActor *a) {
  if (!a || a->NetworkReplica || a->bDeleteMe())
    return false;
#ifdef OPENRUNE_ENABLE_NETWORK
  if (engine->NetworkClient)
    return false;
#endif
  return true;
}
bool live(UActor *a) {
  auto *p = UObject::TryCast<UPawn>(a);
  return p && p->Health() > 0 && !p->bDeleteMe();
}
USkelModel *model(UActor *a) {
  return UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));
}
mat4 orientation(mat4 m) {
  for (int c = 0; c < 3; ++c) {
    vec3 v(m[c * 4], m[c * 4 + 1], m[c * 4 + 2]);
    float n = length(v);
    if (n < .0001f)
      return mat4::identity();
    for (int j = 0; j < 3; ++j)
      m[c * 4 + j] /= n;
  }
  m[12] = m[13] = m[14] = 0;
  m[3] = m[7] = m[11] = 0;
  m[15] = 1;
  return m;
}
Rotation rotationOf(mat4 m) {
  auto q = normalize(quaternion::rotation_matrix(orientation(m)));
  return {q.x, q.y, q.z, q.w};
}
mat4 matrix(Rotation q) { return mat4::quaternion(q.x, q.y, q.z, q.w); }
vec3 point(const mat4 &m) { return {m[12], m[13], m[14]}; }
vec3 localPoint(const mat4 &m, vec3 p) {
  p -= point(m);
  vec3 answer;
  for (int c = 0; c < 3; ++c) {
    vec3 axis(m[c * 4], m[c * 4 + 1], m[c * 4 + 2]);
    answer[c] = dot(axis, p) / std::max(.00001f, dot(axis, axis));
  }
  return answer;
}
void position(UActor *a, vec3 p) {
  auto &c = a->XLevel()->Collision;
  c.RemoveFromCollision(a);
  a->Location() = p;
  a->OldLocation() = p;
  a->UpdateActorZone();
  c.AddToCollision(a);
}
Saved save(UActor *a) {
  return {a->Physics(),
          bool(a->bCollideActors()),
          bool(a->bBlockActors()),
          bool(a->bBlockPlayers()),
          bool(a->bCollideWorld()),
          a->CollisionRadius(),
          a->CollisionHeight()};
}
bool humanoid(USkelModel *m) {
  if (!m || m->Joints.empty() || m->Joints.size() > 64)
    return false;
  int found = 0;
  for (auto &j : m->Joints)
    if (j.Name == "head" || j.Name == "lhip" || j.Name == "rhip" ||
        j.Name == "lwrist" || j.Name == "rwrist")
      ++found;
  return found == 5;
}
bool physicalJoint(const NameString &name) {
  auto n = name.ToString();
  for (char &c : n)
    c = char(std::tolower(c));
  if (n.starts_with("attach") || n.starts_with("attatch"))
    return false;
  return n == "head" || n == "neck" || n == "torso" || n == "pelvis" ||
         n == "chest" || n == "lhip" || n == "rhip" ||
         n.find("should") != std::string::npos ||
         n.find("elbow") != std::string::npos ||
         n.find("knee") != std::string::npos || n == "lwrist" ||
         n == "rwrist" || n.find("ankle") != std::string::npos;
}
bool eligible(UPawn *p) {
  return p && authority(p) && p->Health() > 0 && humanoid(model(p)) &&
         p->DrawScale() > 0 && p->DrawScale() < 2 && p->Mass() < 250 &&
         (!p->HasProperty("bIsBoss") || !p->GetBool("bIsBoss")) &&
         p->GetStateName() != "Scripting" && p->Physics() != PHYS_Swimming &&
         p->Physics() != PHYS_Flying &&
         !(UObject::TryCast<UPlayerPawn>(p) &&
           UObject::Cast<UPlayerPawn>(p)->ViewTarget());
}
std::vector<Triangle> geometry(UModel *m) {
  std::vector<Triangle> out;
  if (!m)
    return out;
  for (auto &n : m->Nodes) {
    if (n.NumVertices < 3 || n.Surf < 0 || size_t(n.Surf) >= m->Surfaces.size())
      continue;
    auto flags = m->Surfaces[n.Surf].PolyFlags;
    if (flags & (PF_NotSolid | PF_FakeBackdrop))
      continue;
    auto vertex = [&](int i) {
      int index = n.VertPool + i;
      if (index < 0 || size_t(index) >= m->Vertices.size())
        return vec3(0);
      auto p = m->Vertices[index].Vertex;
      return p >= 0 && size_t(p) < m->Points.size() ? m->Points[p] : vec3(0);
    };
    auto a = vertex(0);
    for (int i = 1; i + 1 < n.NumVertices; ++i) {
      auto b = vertex(i), c = vertex(i + 1);
      if (length(cross(b - a, c - a)) < .01f)
        continue;
      vec3 normal(n.PlaneX, n.PlaneY, n.PlaneZ);
      if (dot(cross(b - a, c - a), normal) < 0)
        std::swap(b, c);
      out.push_back({a, b, c});
      out.push_back({c, b, a});
    }
  }
  if (out.empty() && m->Polys)
    for (auto &p : m->Polys->Polys)
      if (!(p.PolyFlags & (PF_NotSolid | PF_FakeBackdrop)) &&
          p.Vertices.size() >= 3)
        for (size_t i = 1; i + 1 < p.Vertices.size(); ++i) {
          auto a = p.Vertices[0], b = p.Vertices[i], c = p.Vertices[i + 1];
          out.push_back({a, b, c});
          out.push_back({c, b, a});
        }
  return out;
}
void ensure(ULevel *level) {
  if (currentLevel == level && world)
    return;
  for(auto it=states.begin();it!=states.end();)if(!it->second.replica || !it->first || it->first->XLevel()!=level)it=states.erase(it);else ++it;
  puppets.clear();
  proxies.clear();
  movers.clear();
  hazards.clear();
  retiredBodies.clear();
  resistantUntil.clear();
  currentLevel = level;
  world = std::make_unique<World>();
  auto triangles = geometry(level->Model);
  world->geometry(triangles);
  if (engine->LevelInfo)
    world->gravity(engine->LevelInfo->ZoneGravity());
  accumulator = scan = 0;
  nextGroup = 1;
  LogMessage("RUNE_PHYSICS_WORLD triangles=" +
             std::to_string(triangles.size()));
}
void unpin(State& s) {
  if(!s.impaler)return;
  s.impaler->finisherHeld=false;
  s.impaler->finisherWithdraw=engine->TotalTime;
  if(world){
    world->motion(s.parts[s.pinnedPart].body,Motion::Dynamic);
    for(auto& part:s.parts)world->wake(part.body);
  }
  if(!s.actor->bDeleteMe()){
    if(s.pinLifeSpan>0 && s.actor->LifeSpan()==0)s.actor->LifeSpan()=s.pinLifeSpan;
    if(s.pinTimerRate>0 && s.actor->TimerRate()==0)s.actor->TimerRate()=s.pinTimerRate;
  }
  s.impaler=nullptr;
  s.pinWeapon=s.pinOffhand=nullptr;
  s.pinnedPart=-1;
}
void erase(UActor *a, bool restore) {
  auto it = states.find(a);
  if (it == states.end())
    return;
  auto &s = it->second;
  unpin(s);
  auto at = a->Location();
  if (restore && !s.replica && world && s.prop)
    at =
        world->get(s.parts[0].body).position - (s.bindLocation - s.actorOrigin);
  if (restore && !s.replica && s.living) {
    vec3 floor, normal;
    if (a->Trace(floor, normal, at - vec3(0, 0, 180), at + vec3(0, 0, 60),
                 false, vec3(0)) &&
        normal.z > .7f) {
      auto candidate = a->CheckLocation(floor + vec3(0, 0, s.saved.height + 1),
                                        s.saved.radius, s.saved.height, true);
      if (candidate.first)
        at = candidate.second;
    }
    auto clear = a->CheckLocation(at, s.saved.radius, s.saved.height, true);
    if (clear.first)
      at = clear.second;
    else {
      auto fallback =
          a->CheckLocation(s.actorOrigin, s.saved.radius, s.saved.height, true);
      if (fallback.first)
        at = fallback.second;
    }
  }
  if (world && !s.replica)
    for (auto &p : s.parts)
      world->remove(p.body);
  if (restore && !s.replica && a && !a->bDeleteMe()) {
    if (s.prop && s.carrier)
      a->SetOwner(s.originalOwner);
    a->SetCollision(s.saved.collide, s.saved.blockActors, s.saved.blockPlayers);
    a->bCollideWorld() = s.saved.world;
    a->SetCollisionSize(s.saved.radius, s.saved.height);
    position(a, at);
    a->SetPhysics(s.saved.world ? PHYS_Falling : s.saved.physics);
    if (s.living && live(a))
      if (auto *p = UObject::TryCast<UPawn>(a)) {
        p->GotoState(p->IsA("RunePlayer") ? "PlayerWalking" : "Fighting",
                     "Begin");
        if (auto *proxy = p->GetUObject("AnimProxy"))
          proxy->GotoState("Idle", "Begin");
        resistantUntil[p] = simulationTime + 3;
      }
  }
  states.erase(it);
}
bool reserveBody(bool living) {
  size_t active = 0, corpses = 0, awakePawns = 0;
  UActor *oldest = nullptr;
  float age = -1;
  for (auto &[a, s] : states)
    if (!s.replica) {
      ++active;
      if (s.living)
        ++awakePawns;
      else if (!s.prop) {
        ++corpses;
        if (!s.impaler && s.age > age) {
          age = s.age;
          oldest = a;
        }
      }
    }
  if (living && awakePawns >= 16)
    return false;
  if ((!living && corpses >= 12) || active >= 40) {
    if (!oldest)
      return false;
    retiredBodies.insert(oldest);
    erase(oldest, true);
  }
  return true;
}
bool finiteVector(vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool finiteRotation(Rotation r) {
  double n=double(r.x)*r.x+double(r.y)*r.y+double(r.z)*r.z+double(r.w)*r.w;
  return std::isfinite(n)&&n>1e-12&&n<1e30;
}
bool finiteMotion(const RuneRigid::Transform& t) {
  return finiteVector(t.position)&&finiteVector(t.velocity)&&finiteVector(t.angular)&&finiteRotation(t.rotation);
}
bool restoreMotion(State& s) {
  for(const auto& part:s.parts)if(!finiteMotion(part.lastValid))return false;
  for(auto& part:s.parts) {
    if(!world->recover(part.body,part.lastValid))return false;
    if(!s.living)world->sleep(part.body);
    part.current=part.lastValid;part.current.velocity=part.current.angular=vec3(0);part.lastValid=part.current;
  }
  s.actor->Velocity()=vec3(0);
  LogMessage("RUNE_PHYSICS_MOTION_RECOVERED actor="+s.actor->Name.ToString()+" model="+(s.model?s.model->Name.ToString():"prop")+" age="+std::to_string(s.age));
  return true;
}
bool validateMotion(State& s) {
  for(const auto& part:s.parts)if(!world->contains(part.body)||!finiteMotion(part.current)) {
    // A failed cosmetic solve is discarded, never repaired by writing gameplay velocity.
    if(s.driven)return false;
    const auto& t=part.current;
    LogMessage("RUNE_PHYSICS_INVALID_MOTION actor="+s.actor->Name.ToString()+" joint="+std::to_string(part.joint)+" age="+std::to_string(s.age)+" velocity="+std::to_string(t.velocity.x)+","+std::to_string(t.velocity.y)+","+std::to_string(t.velocity.z)+" rotation="+std::to_string(t.rotation.x)+","+std::to_string(t.rotation.y)+","+std::to_string(t.rotation.z)+","+std::to_string(t.rotation.w));
    return restoreMotion(s);
  }
  for(auto& part:s.parts)part.lastValid=part.current;
  return true;
}
bool readMotion(State& s) {
  for(auto& part:s.parts)part.current=world->get(part.body);
  return validateMotion(s);
}

void pose(State &s,bool prepared=false) {
  CurrentScope evaluateCurrent;
  if (s.prop || !s.model || s.parts.empty())
    return;
  auto *a = s.actor;
  if(!prepared&&!readMotion(s))return;
  auto root=s.parts[0].current;
  // Contacts can apply impulses after the validated transform read. Preserve
  // that latest root velocity without repeating every bone transform query.
  if(prepared){auto current=world->get(s.parts[0].body);if(finiteMotion(current))root.velocity=current.velocity;else if(restoreMotion(s))root=s.parts[0].current;else return;}
  auto delta = multiply(root.rotation, inverse(s.parts[0].bindBody));
  auto origin =
      root.position + rotate(delta, s.bindLocation - s.parts[0].bindCenter);
  if (s.recovering) {
    float t = std::clamp(s.recovery / (puppetEnabled()?.6f:.45f), 0.f, 1.f);
    origin = mix(s.recoveryStart, s.recoveryLocation, t * t * (3 - 2 * t));
  }
  position(a, origin);
  a->Velocity() = root.velocity;
  auto meshWorld = GetRuneMeshToWorld(a);
  s.pose = s.rest;
  Array<mat4> globals;
  globals.resize(s.pose.size());
  std::map<int, Part *> indexed;
  for (auto &p : s.parts)
    indexed[p.joint] = &p;
  for (size_t i = 0; i < s.pose.size(); ++i) {
    auto it = indexed.find(int(i));
    auto parent = s.model->Joints[i].Parent;
    if (it != indexed.end()) {
      auto &part = *it->second;
      auto physical = part.current;
      auto desired = multiply(
          multiply(physical.rotation, inverse(part.bindBody)), part.bindJoint);
      auto parentWorld = i ? meshWorld * globals[parent] : meshWorld;
      auto local = multiply(inverse(rotationOf(parentWorld)), desired);
      auto r = matrix(local);
      s.pose[i].Rotation = RunePoseEuler(i ? r : mat4::transpose(r));
    }
    if (i == 0) {
      auto desired = root.position + rotate(delta, s.parts[0].bindPoint -
                                                       s.parts[0].bindCenter);
      s.pose[0].Position = localPoint(meshWorld, desired);
    }
    auto r = RunePoseRotation(s.pose[i].Rotation);
    if (!i)
      r = mat4::transpose(r);
    auto local =
        mat4::translate(s.pose[i].Position) * r * mat4::scale(s.pose[i].Scale);
    globals[i] = i ? globals[parent] * local : local;
  }
  if (s.recovering) {
    float t = std::clamp(s.recovery / (puppetEnabled()?.6f:.45f), 0.f, 1.f);
    t = t * t * (3 - 2 * t);
    for (size_t i = 0; i < s.pose.size(); ++i)
      s.pose[i] = BlendRunePose(s.pose[i], s.targetPose.size()==s.pose.size()?s.targetPose[i]:s.rest[i], t);
  }
  UpdateRuneAttachments(a);
}
bool friends(UPawn *a, UPawn *b) {
  if (!a || !b)
    return false;
  auto *ar = a->PlayerReplicationInfo();
  auto *br = b->PlayerReplicationInfo();
  if (ar && br && engine->GameInfo &&
      ((engine->GameInfo->IsA("TeamGame") &&
        engine->GameInfo->GetFloat("FriendlyFireScale") == 0) ||
       engine->GameInfo->IsA("ArenaGameInfo")))
    return ar->GetByte("Team") == br->GetByte("Team");
  return a == b ||
         (!a->IsA("RunePlayer") && !b->IsA("RunePlayer") && a->Enemy() != b);
}
bool playingCombat() {
  return !engine->GameInfo || !engine->GameInfo->IsA("HeadBallGame") ||
         engine->GameInfo->GetBool("bGameStarted");
}
bool inLevel(UActor *a) {
  return a &&
         std::find(currentLevel->Actors.begin(), currentLevel->Actors.end(),
                   a) != currentLevel->Actors.end() &&
         !a->bDeleteMe();
}
void impactEffect(UActor *a, vec3 point, vec3 normal, bool wood) {
  if (simulationTime < nextImpactEffect)
    return;
  nextImpactEffect = simulationTime + .12;
  if (auto *cls = engine->packages->FindClass(wood ? "RuneI.HitWood"
                                                   : "RuneI.HitStone")) {
    auto *effect = a->Spawn(cls, {}, {}, point + normal * 3.f,
                            Rotator::FromVector(normal));
    if (effect)
      effect->LifeSpan() =
          std::min(effect->LifeSpan() > 0 ? effect->LifeSpan() : 1.f, 1.f);
  }
}
void wallImpact(UActor *a, const Contact &c, UActor *surface) {
  auto it = states.find(a);
  if (it == states.end() || !it->second.living || it->second.replica ||
      it->second.wallSlamDone || it->second.age < .08f || c.speed < 300 ||
      std::abs(c.normal.z) > .4f ||
      (surface && (!inLevel(surface) || !GetMovingBrush(surface))))
    return;
  auto *target = UObject::TryCast<UPawn>(a);
  auto *source = it->second.instigator;
  if (!live(target) || !inLevel(source) || !live(source) ||
      friends(source, target) || !playingCombat())
    return;
  // One bounded consequence per knockdown; limb contacts cannot stack damage
  // or keep a player locked against a wall. Floors and shallow scrapes
  // excluded.
  auto &s = it->second;
  s.wallSlamDone = true;
  s.recovering = false;
  s.recovery = 0;
  s.stunUntil = std::max(s.stunUntil, std::max(1.f, s.age) + .45f);
  s.lastImpact = simulationTime;
  int damage = std::clamp(int((c.speed - 260) * .025f), 3, 14);
  ++wallSlams;
  auto outward =
      a == reinterpret_cast<UActor *>(uintptr_t(c.a)) ? -c.normal : c.normal;
  impactEffect(target, c.point, outward, false);
  if (target->HasProperty("LandSoundStone"))
    if (auto *sound = target->GetUObject("LandSoundStone"))
      CallEvent(target, "PlaySound",
                {ExpressionValue::ObjectValue(sound),
                 ExpressionValue::IntValue(1),
                 ExpressionValue::FloatValue(.95f)});
  CallEvent(
      target, "JointDamaged",
      {ExpressionValue::IntValue(damage), ExpressionValue::ObjectValue(source),
       ExpressionValue::VectorValue(c.point),
       ExpressionValue::VectorValue(vec3(0)),
       ExpressionValue::NameValue("blunt"), ExpressionValue::IntValue(0)});
  LogMessage("RUNE_PHYSICS_WALL_SLAM target=" + target->Name.ToString() +
             " speed=" + std::to_string(c.speed) +
             " damage=" + std::to_string(damage));
}
bool breakProp(UActor *a, const Contact &c, UActor *other) {
  auto it = states.find(a);
  if (it == states.end() || !it->second.prop || it->second.replica ||
      it->second.carrier || !SafeProp(a) || a->bNoDelete() ||
      !a->GetBool("bDestroyable"))
    return false;
  auto &s = it->second;
  UPawn *source = s.impactArmed ? s.instigator : nullptr;
  if (!source)
    if (auto hit = states.find(other);
        hit != states.end() && (hit->second.impactArmed || hit->second.living))
      source = hit->second.instigator;
  if (!inLevel(source) || !live(source) || !playingCombat() ||
      c.speed < (a->IsA("Keg") ? 360.f : 280.f))
    return false;
  vec3 momentum = a == reinterpret_cast<UActor *>(uintptr_t(c.a)) ? c.velocityA
                                                                  : c.velocityB;
  if (length(momentum) < 80)
    momentum = (a == reinterpret_cast<UActor *>(uintptr_t(c.a)) ? c.velocityB
                                                                : c.velocityA);
  if (length(momentum) < 1)
    momentum = vec3(0, 0, 1);
  // Let original DecorationRune callbacks produce their sound/debris and
  // destroy the actor; do not fabricate a second broken object or map event.
  auto point = c.point;
  auto outward =
      a == reinterpret_cast<UActor *>(uintptr_t(c.a)) ? -c.normal : c.normal;
  impactEffect(a, point, outward, true);
  CallEvent(
      a, "JointDamaged",
      {ExpressionValue::IntValue(50), ExpressionValue::ObjectValue(source),
       ExpressionValue::VectorValue(point),
       ExpressionValue::VectorValue(momentum),
       ExpressionValue::NameValue("blunt"), ExpressionValue::IntValue(0)});
  if (a->bDeleteMe()) {
    ++brokenProps;
    LogMessage("RUNE_PHYSICS_PROP_BREAK actor=" + a->Name.ToString() +
               " speed=" + std::to_string(c.speed));
    return true;
  }
  return false;
}
void contacts() {
  const bool enableImpacts =
      commandline->GetArgInt("", "--physics-impacts", 1) != 0;
  std::map<UActor *, Contact> strongest, wallContacts;
  for (auto &c : world->contacts()) {
    if(puppets.contains(reinterpret_cast<UActor*>(uintptr_t(c.a))) || puppets.contains(reinterpret_cast<UActor*>(uintptr_t(c.b))))continue;
    if(!std::isfinite(c.speed)||!finiteVector(c.point)||!finiteVector(c.normal)||!finiteVector(c.velocityA)||!finiteVector(c.velocityB))continue;
    if (enableImpacts)
      for (auto token : {c.a, c.b}) {
        auto *actor = reinterpret_cast<UActor *>(uintptr_t(token));
        auto *other =
            reinterpret_cast<UActor *>(uintptr_t(token == c.a ? c.b : c.a));
        auto moving = states.find(actor), struck = states.find(other);
        bool driven = moving != states.end() &&
                      (moving->second.impactArmed ||
                       (struck != states.end() &&
                        (struck->second.impactArmed || struck->second.living)));
        if (moving != states.end() && moving->second.prop && driven &&
            (!strongest.contains(actor) || strongest[actor].speed < c.speed))
          strongest[actor] = c;
        if (states.contains(actor) && (!other || movers.contains(other)) &&
            std::abs(c.normal.z) < .4f &&
            (!wallContacts.contains(actor) ||
             wallContacts[actor].speed < c.speed))
          wallContacts[actor] = c;
      }

    auto *a = reinterpret_cast<UActor *>(uintptr_t(c.a));
    auto *b = reinterpret_cast<UActor *>(uintptr_t(c.b));
    auto deal = [&](UActor *moving, UActor *other) {
      auto it = states.find(moving);
      if (it == states.end() || c.speed < 220)
        return;
      auto &s = it->second;
      if (s.prop && (s.carrier || !s.impactArmed))
        return;
      if (c.speed > 240 && simulationTime > s.lastImpact + .5) {
        s.lastImpact = simulationTime;
        auto *profile = UObject::TryCast<UPawn>(moving);
        if (!profile)
          profile = s.instigator;
        if (!inLevel(profile))
          profile = nullptr;
        if (profile && profile->HasProperty("LandSoundFlesh"))
          if (auto *sound = profile->GetUObject("LandSoundFlesh"))
            CallEvent(moving, "PlaySound",
                      {ExpressionValue::ObjectValue(sound),
                       ExpressionValue::IntValue(1),
                       ExpressionValue::FloatValue(
                           std::clamp(c.speed / 700.f, .25f, .8f))});
      }
      auto *target =
          std::find(currentLevel->Actors.begin(), currentLevel->Actors.end(),
                    other) != currentLevel->Actors.end()
              ? UObject::TryCast<UPawn>(other)
              : nullptr;
      if (!target || !live(target) || !inLevel(s.instigator) ||
          !live(s.instigator) || friends(s.instigator, target) ||
          s.victims.contains(target))
        return;
      if (engine->GameInfo && engine->GameInfo->IsA("HeadBallGame") &&
          !engine->GameInfo->GetBool("bGameStarted"))
        return;
      if (std::find(currentLevel->Actors.begin(), currentLevel->Actors.end(),
                    target) == currentLevel->Actors.end())
        return;
      bool blocked = RuneKick::ShieldBlocks(target, s.instigator);
      bool crossed = !blocked && RuneDualWield::Blocks(target, s.instigator);
      s.victims.insert(target);
      if (blocked) {
        auto d = world->get(s.parts[0].body).velocity;
        d.z = 0;
        if (length(d) > .01f) {
          target->RuneKickRecoil = normalize(d) * 6.f;
          target->RuneKickRecoilTime = .12f;
        }
        RuneFeedback::Impact(s.instigator, target, true, false);
        return;
      }
      int damage = std::clamp(int((c.speed - 180) * .035f), 2, 35);
      if (crossed) {
        damage = std::max(1, damage / 3);
        RuneDualWield::BreakGuard(target, .25f);
      }
      int before = target->Health();
      auto force = world->get(s.parts[0].body).velocity;
      auto *source = s.instigator;
      CallEvent(target, "JointDamaged",
                {ExpressionValue::IntValue(damage),
                 ExpressionValue::ObjectValue(s.instigator),
                 ExpressionValue::VectorValue(c.point),
                 ExpressionValue::VectorValue(vec3(0)),
                 ExpressionValue::NameValue("blunt"),
                 ExpressionValue::IntValue(0)});
      if (target->Health() < before) {
        if (crossed) {
          auto d = force;
          d.z = 0;
          if (length(d) > .01f) {
            target->RuneKickRecoil = normalize(d) * 22.f;
            target->RuneKickRecoilTime = .18f;
          }
        } else
          Impact(source, target, force * .55f + vec3(0, 0, 80), false);
      }
      RuneFeedback::Impact(source, target, crossed, false);
    };
    deal(a, b);
    deal(b, a);
  }
  for (auto &[actor, c] : wallContacts) {
    auto *other = reinterpret_cast<UActor *>(
        uintptr_t(c.a == uint64_t(uintptr_t(actor)) ? c.b : c.a));
    if (inLevel(actor))
      wallImpact(actor, c, other);
  }
  unsigned breaks = 0;
  for (auto &[actor, c] : strongest) {
    auto *other = reinterpret_cast<UActor *>(
        uintptr_t(c.a == uint64_t(uintptr_t(actor)) ? c.b : c.a));
    if (!inLevel(actor))
      continue;
    if (breaks < 2 && breakProp(actor, c, other))
      ++breaks;
  }
}
} // namespace
PresentationScope::PresentationScope() { ++presentationDepth; }
PresentationScope::~PresentationScope() { --presentationDepth; }
int mode() { return applied; }
bool RepairInvalidMotion(UActor* actor) {
  auto it=states.find(actor);
  if(it==states.end()||it->second.replica||it->second.parts.empty()||!world)return false;
  auto& state=it->second;
  if(!readMotion(state))return false;
  if(state.prop){position(actor,state.parts[0].current.position);actor->Velocity()=state.parts[0].current.velocity;}
  else pose(state,true);
  LogMessage("RUNE_PHYSICS_ACTOR_MOTION_REPAIRED actor="+actor->Name.ToString());
  return finiteVector(actor->Velocity())&&finiteVector(actor->Location());
}

void Reset() {
  RuneIK::Reset();
  puppets.clear();
  pendingDeaths.clear();
  deathObservations.clear();
  states.clear();
  proxies.clear();
  movers.clear();
  hazards.clear();
  retiredBodies.clear();
  resistantUntil.clear();
  world.reset();
  currentLevel = nullptr;
  accumulator = scan = 0;
  counters = {};
  wallSlams = brokenProps = 0;
  nextImpactEffect = 0;
}
bool InPresentation(){return presentationDepth>0 && currentEvaluation==0;}
bool Controls(UActor *a) { return states.contains(a); }
bool SafeProp(UActor *a) {
  if (!a || a->bDeleteMe() || a->bStatic() || a->bHidden() ||
      !a->Event().IsNone() || a->Tag() != a->Class->Name ||
      a->GetUObject("AttachParent"))
    return false;
  return a->Class->Name == "Stool" || a->Class->Name == "StoolDwarf" ||
         a->Class->Name == "Keg" || a->Class->Name == "Bucket";
}
void Hazard(UActor *a, int kind) {
  if (a && authority(a) && kind >= 1 && kind <= 2)
    hazards[a] = {kind, {}};
}
bool IsProp(UActor *a) {
  auto it = states.find(a);
  return it != states.end() && it->second.prop;
}
bool Carrying(UPawn *p) {
  for (auto &[a, s] : states)
    if (s.carrier == p)
      return true;
  return false;
}
bool ApplyPose(UActor *a, Array<RuneJointPose> &p) {
  if(RuneAnimation::TargetDepth)return false;
  if(InPresentation())if(auto it=puppets.find(a);it!=puppets.end()) {
    auto& s=it->second;
    if(p.size()!=s.pose.size() || !live(a) || model(a)!=s.model)return false;
    if(!s.puppetPresented && commandline->HasArg("","--rune-puppet-trace") && commandline->HasArg("","--save-directory")) {
      s.puppetPresented=true;LogMessage("RUNE_PUPPET_VISIBLE actor="+a->Name.ToString()+" replica="+std::to_string(a->NetworkReplica));
    }
    float t=std::clamp((s.age-.12f)/.58f,0.f,1.f),weight=.7f*(1-t*t*(3-2*t));
    for(int j:s.activeJoints)if(j>0 && size_t(j)<p.size()) {
      auto physical=s.pose[j];
      if(s.previousPose.size()==p.size())physical=BlendRunePose(s.previousPose[j],physical,renderAlpha());
      // Presentation rotation only. Authored bone lengths, world movement,
      // collision, attack sweeps and the block window retain their own state.
      auto blended=BlendRunePose(p[j],physical,weight);p[j].Rotation=blended.Rotation;
    }
    return true;
  }
  auto it = states.find(a);
  if (it == states.end() || it->second.prop ||
      p.size() != it->second.pose.size())
    return false;
  auto &s = it->second;
  p = s.pose;
  if (!s.replica && s.previousPose.size() == p.size() && renderAlpha() < 1)
    for (size_t j = 0; j < p.size(); ++j)
      p[j] = BlendRunePose(s.previousPose[j], p[j], renderAlpha());
  return true;
}
bool CustomTransform(UActor *a) {
  auto it = states.find(a);
  return it != states.end();
}
vec3 RenderOffset(UActor *a) {
  auto it = states.find(a);
  if (it == states.end() || it->second.replica || it->second.prop)
    return vec3(0);
  return mix(it->second.previousLocation, a->Location(),
             currentEvaluation ? 1.f
                               : std::clamp(accumulator * 60.f, 0.f, 1.f)) -
         a->Location();
}
mat4 MeshToWorld(UActor *a) {
  auto &s = states.at(a);
  if (!s.prop) {
    auto *m = model(a);
    return mat4::translate(presentationDepth ? RenderOffset(a) : vec3(0)) *
           mat4::translate(m ? m->PositionOffset : vec3(0)) *
           GetRuneActorToWorld(a) *
           (m ? Coords::Rotation(m->RotationOffset).ToMatrix()
              : mat4::identity());
  }
  if (s.replica)
    return s.replicaWorld;
  auto t = s.currentBody;
  float alpha = renderAlpha();
  t.position = mix(s.previousBody.position, t.position, alpha);
  auto asQuat = [](Rotation q) { return quaternion(q.x, q.y, q.z, q.w); };
  auto q = normalize(
      slerp(asQuat(s.previousBody.rotation), asQuat(t.rotation), alpha));
  t.rotation = {q.x, q.y, q.z, q.w};
  auto delta = multiply(t.rotation, inverse(s.parts[0].bindBody));
  return mat4::translate(t.position) * matrix(delta) *
         mat4::translate(-s.bindLocation) * s.bindWorld;
}
static bool makeRagdoll(UActor *a, vec3 velocity, UPawn *instigator,
                        bool living, const DeathSeed *seed, bool combatOnly = false, bool puppet = false) {
  auto* rig=model(a);
  const bool creature=rig && combatOnly && !living && a->IsA("Carcass") && !humanoid(rig) && rig->Joints.size()>=2 && rig->Joints.size()<=64;
  if ((!puppet && ((living&&!RuneBattleLaws::Allowed(RuneBattleLaws::Knockdown)) || (!applied && !combatOnly) || !authority(a))) || !a->XLevel() || Controls(a) ||
      (!creature && !humanoid(rig)) || a->bCarriedItem())
    return false;
  if(!finiteVector(velocity)){LogMessage("RUNE_PHYSICS_INVALID_SEED_VELOCITY actor="+a->Name.ToString());velocity=vec3(0);}
  ensure(a->XLevel());
  if (!puppet && !reserveBody(living))
    return false;
  clearPuppet(a);
  retiredBodies.erase(a);
  auto *m = model(a);
  State s;
  s.actor = a;
  s.model = m;
  s.saved = save(a);
  s.living = living;
  s.driven = puppet;
  s.instigator = instigator;
  s.impactArmed = instigator && length(velocity) > 150;
  s.bindLocation = a->Location();
  s.actorOrigin = a->Location();
  s.bindWorld = GetRuneMeshToWorld(a);
  s.bindActor = rotationOf(s.bindWorld);
  {RuneAnimation::TargetPoseScope target;s.rest = seed ? seed->pose : m->ActorPose(a);}
  if (!seed) {
    auto reference = RuneCorpse::ReferencePose(*m);
    for (size_t j = 1; j < s.rest.size(); ++j) {
      s.rest[j].Position = reference[j].Position;
      s.rest[j].Scale = reference[j].Scale;
    }
  }

#ifdef OPENRUNE_ENABLE_NETWORK
  if (seed) {
    auto reference = RuneCorpse::ReferencePose(*m);
    for(size_t j=0;j<s.rest.size();++j) {
      bool changed=length(s.rest[j].Scale-reference[j].Scale)>1.f/8192 ||
          (j && length(s.rest[j].Position-reference[j].Position)>1.f/128);
      if(!changed)continue;
      openrune::net::PhysicalBasis b;b.joint=uint8_t(j);
      for(int k=0;k<3;++k) {
        float pos=j?s.rest[j].Position[k]:reference[j].Position[k];
        if(!std::isfinite(pos) || std::abs(pos)>511.f)return false;
        b.position[k]=int16_t(std::lround(pos*64));
        b.scale[k]=uint16_t(std::clamp(std::lround(s.rest[j].Scale[k]*4096),1l,16384l));
      }
      s.basis.push_back(b);
    }
  }
#endif
  s.pose = s.rest;
  auto bones = m->JointMatrices(s.rest);
  uint32_t group = nextGroup++;
  std::map<int, int> indices;
  for (size_t i = 0; i < m->Joints.size() && s.parts.size() < 20; ++i) {
    if (i && !physicalJoint(m->Joints[i].Name) && (!creature || std::none_of(std::begin(m->Joints[i].Children),std::end(m->Joints[i].Children),[&](int j){return j>int(i) && size_t(j)<m->Joints.size();})))
      continue;
    auto name = m->Joints[i].Name.ToString();
    if (name.starts_with("attach") || name.starts_with("attatch"))
      continue;
    auto start = point(s.bindWorld * bones[i]);
    int child = -1;
    for (int j : m->Joints[i].Children)
      if (j > int(i) && size_t(j) < m->Joints.size()) {
        auto n = m->Joints[j].Name.ToString();
        if (!n.starts_with("attach") && !n.starts_with("attatch")) {
          child = j;
          break;
        }
      }
    auto end = child >= 0 ? point(s.bindWorld * bones[child])
                          : start + vec3(0, 0, 3 * a->DrawScale());
    float distance = length(end - start);
    float radius =
        std::clamp(distance * .22f, 2.f * a->DrawScale(), 7.f * a->DrawScale());
    if (name == "torso" || name == "pelvis" || name == "chest")
      radius = 7 * a->DrawScale();
    if (name == "head")
      radius = 5 * a->DrawScale();
    Description d;
    d.position = (start + end) * .5f;
    if(puppet){d.motion=Motion::Kinematic;d.cosmetic=true;}
    d.capsule = true;
    d.radius = radius;
    d.halfHeight = std::max(.8f, distance * .5f - radius);
    d.rotation = between({0, 1, 0}, end - start);
    d.velocity = velocity;
    if(!finiteVector(start)||!finiteVector(end)||!finiteVector(d.position)||!finiteRotation(d.rotation)||!finiteRotation(rotationOf(s.bindWorld*bones[i]))) {
      for(auto& part:s.parts)world->remove(part.body);
      LogMessage("RUNE_PHYSICS_INVALID_SEED actor="+a->Name.ToString()+" model="+m->Name.ToString()+" joint="+name);return false;
    }
    d.mass = std::max(2.f, a->Mass() / 14.f);
    d.owner = uint64_t(uintptr_t(a));
    d.group = group;
    d.friction = .8f;
    d.restitution = applied == Chaos ? .18f : .06f;
    auto body = world->create(d);
    if (body == Invalid) {
      for (auto &part : s.parts)
        world->remove(part.body);
      return false;
    }
    int parent = m->Joints[i].Parent;
    while (i && parent >= 0 && !indices.contains(parent))
      parent = m->Joints[parent].Parent;
    int parentPart = i && parent >= 0 ? indices[parent] : -1;
    if (parentPart >= 0) {
      float swing = 1.15f, twist = .45f;
      if (name == "pelvis" || name == "torso" || name == "chest") {
        swing = .32f;
        twist = .22f;
      } else if (name == "neck" || name == "head") {
        swing = .55f;
        twist = .45f;
      } else if (name.find("knee") != std::string::npos) {
        swing = 1.f;
        twist = .10f;
      } else if (name.find("elbow") != std::string::npos) {
        swing = 1.25f;
        twist = .12f;
      } else if (name.find("ankle") != std::string::npos ||
                 name.find("wrist") != std::string::npos) {
        swing = .5f;
        twist = .25f;
      }
      world->constrain(s.parts[parentPart].body, body, start, end - start,
                       swing, twist);
    }
    indices[int(i)] = int(s.parts.size());
    s.parts.push_back({body, int(i), parentPart, d.rotation,
                       rotationOf(s.bindWorld * bones[i]), d.position, start});
    s.parts.back().current=s.parts.back().lastValid=world->get(body);
  }
  if (s.parts.size() < (creature?2u:6u)) {
    for (auto &p : s.parts)
      world->remove(p.body);
    return false;
  }
  if(puppet) {
    s.previousPose=s.pose;s.puppetOrigin=a->Location();s.puppetRotation=a->Rotation();
    puppets.emplace(a,std::move(s));return true;
  }
  // A shove above the centre of mass should tumble the whole articulated
  // body, rather than translating an upright pose until it meets a wall.
  if (!seed && length(velocity) > 150) {
    vec3 center(0);
    for (auto &p : s.parts)
      center += p.bindCenter;
    center /= float(s.parts.size());
    auto axis = vec3(velocity.y, -velocity.x, 0);
    if (length(axis) < 1)
      axis = {0, 1, 0};
    auto spin = normalize(axis) * (living ? 3.4f : 2.6f) *
                    (applied == Chaos ? 1.35f : 1.f) +
                vec3(0, 0, .45f);
    for (auto &part : s.parts) {
      world->angularVelocity(part.body, spin);
      world->velocity(part.body,
                      velocity + cross(spin, part.bindCenter - center));
    }
  }

  if (auto proxy = proxies.find(a); proxy != proxies.end()) {
    world->remove(proxy->second);
    proxies.erase(proxy);
  }
  if (instigator)
    world->ignore(uint64_t(uintptr_t(a)), uint64_t(uintptr_t(instigator)),
                  .35f);
  a->SetBase(nullptr, true);
  a->SetPhysics(PHYS_None);
  a->SetCollision(true, false, false);
  a->bCollideWorld() = false;
  // The articulated pose replaces the old centred carry/throw transform.
  // Capture the old pose first, then switch basis before the first pose solve.
  a->CorpseBody.active = a->CorpseBody.flying = false;
  states.emplace(a, std::move(s));
  pose(states.at(a));
  states.at(a).previousPose = states.at(a).pose;
  states.at(a).previousLocation = a->Location();
  if (seed && seed->resting)
    for (auto &part : states.at(a).parts)
      world->sleep(part.body);
  LogMessage("RUNE_PHYSICS_RAGDOLL actor=" + a->Name.ToString() +
             " parts=" + std::to_string(states.at(a).parts.size()) +
             " living=" + std::to_string(living)+" model="+m->Name.ToString()+" mesh="+std::to_string(a->GetByte("SkelMesh")));
  return true;
}
namespace {
struct MuscleTarget {vec3 center;Rotation rotation;};
std::vector<MuscleTarget> muscleTargets(State& s,const Array<RuneJointPose>& target,const mat4& meshWorld) {
  auto bones=s.model->JointMatrices(target);std::vector<MuscleTarget> out;out.reserve(s.parts.size());
  for(auto& part:s.parts) {
    auto delta=multiply(rotationOf(meshWorld*bones[part.joint]),inverse(part.bindJoint));
    out.push_back({point(meshWorld*bones[part.joint])+rotate(delta,part.bindCenter-part.bindPoint),multiply(delta,part.bindBody)});
  }
  return out;
}
void mapPuppet(State& s) {
  auto meshWorld=GetRuneMeshToWorld(s.actor);s.pose=s.targetPose;
  Array<mat4> globals;globals.resize(s.pose.size());std::map<int,Part*> indexed;
  for(auto& part:s.parts)if(s.activeJoints.contains(part.joint))indexed[part.joint]=&part;
  for(size_t i=0;i<s.pose.size();++i) {
    int parent=s.model->Joints[i].Parent;
    if(auto it=indexed.find(int(i));it!=indexed.end() && i) {
      auto& part=*it->second;auto desired=multiply(multiply(part.current.rotation,inverse(part.bindBody)),part.bindJoint);
      auto local=multiply(inverse(rotationOf(meshWorld*globals[parent])),desired);
      s.pose[i].Rotation=RunePoseEuler(matrix(local));
    }
    auto local=mat4::translate(s.pose[i].Position)*(i?RunePoseRotation(s.pose[i].Rotation):mat4::transpose(RunePoseRotation(s.pose[i].Rotation)))*mat4::scale(s.pose[i].Scale);
    globals[i]=i?globals[parent]*local:local;
  }
}
bool DrivePuppet(State& s,float dt) {
  auto* a=s.actor;
  s.previousPose=s.pose;
      {RuneAnimation::TargetPoseScope target;s.targetPose=s.model->ActorPose(a);}
      if(s.targetPose.size()!=s.rest.size())return false;
      auto target=muscleTargets(s,s.targetPose,GetRuneMeshToWorld(a));
      for(size_t i=0;i<s.parts.size();++i) {
        auto& part=s.parts[i];
        if(!s.activeJoints.contains(part.joint))world->kinematic(part.body,target[i].center,target[i].rotation,dt);
        else if(part.parent>=0) {
          auto relative=multiply(inverse(target[part.parent].rotation),target[i].rotation);
          // Soft at contact, firmer during recovery. Limited torque keeps
          // muscles responsive to an impact instead of freezing the animation.
          float strength=std::clamp(s.age/.2f,0.f,1.f);
          world->drive(part.body,relative,5.f+5.f*strength,1.f,35.f+105.f*strength,true);
        }
      }
  return true;
}
void StepPuppets(float dt,bool before) {
  std::vector<UActor*> finish;
  for(auto& [a,s]:puppets) {
    if(!a || a->bDeleteMe() || !live(a) || model(a)!=s.model || Controls(a) || a->bHidden() ||
       a->XLevel()!=currentLevel || RuneCorpse::Busy(UObject::TryCast<UPawn>(a)) ||
       length(a->Location()-s.puppetOrigin)>100 || std::abs(int16_t(a->Rotation().Yaw-s.puppetRotation.Yaw))>16384 || s.age>=.7f || !puppetEnabled()) {finish.push_back(a);continue;}
    if(before) {
      if(!DrivePuppet(s,dt))finish.push_back(a);
    } else {
      if(!readMotion(s)){finish.push_back(a);continue;}
      mapPuppet(s);s.age+=dt;s.puppetOrigin=a->Location();s.puppetRotation=a->Rotation();
    }
  }
  for(auto* a:finish)clearPuppet(a);
}
void DriveRecovery(State& s,float dt) {
  RuneAnimation::TargetPoseScope targetScope;
  // Recover to an actual standing animation, with feet fitted to the validated
  // floor. Never sample the previous simulated result as a new muscle target.
  s.targetPose=RuneCorpse::ReferencePose(*s.model);
  auto meshWorld=mat4::translate(s.recoveryLocation-s.actor->Location())*GetRuneMeshToWorld(s.actor);
  RuneIK::ApplyAtWorld(*s.model,s.actor,s.targetPose,meshWorld);
  auto target=muscleTargets(s,s.targetPose,meshWorld);
  float t=std::clamp((s.recovery+dt)/.6f,0.f,1.f);t=t*t*(3-2*t);
  auto& root=s.parts[0];auto from=s.recoveryBody.rotation,to=target[0].rotation;
  auto rotation=normalize(slerp(quaternion(from.x,from.y,from.z,from.w),quaternion(to.x,to.y,to.z,to.w),t));
  world->kinematic(root.body,mix(s.recoveryBody.position,target[0].center,t),{rotation.x,rotation.y,rotation.z,rotation.w},dt);
  for(size_t i=1;i<s.parts.size();++i) {
    auto& part=s.parts[i];if(part.parent<0)continue;
    world->wake(part.body);
    world->drive(part.body,multiply(inverse(target[part.parent].rotation),target[i].rotation),8.f,1.f,180.f,true);
  }
}
} // namespace
bool React(UPawn* p,vec3 hit,vec3 momentum,int joint) {
  if(!engine || !engine->LaunchInfo.IsRune() || !puppetEnabled() || !p || !live(p) || p->bHidden() ||
     p->XLevel()!=engine->Level || Controls(p) || !humanoid(model(p)) || p->DrawScale()<=0 || p->DrawScale()>2 ||
     p->GetStateName()=="Scripting" || (p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")) ||
     (p->Physics()!=PHYS_Walking && p->Physics()!=PHYS_Falling && !(p->NetworkReplica && p->Physics()==PHYS_None)) || RuneCorpse::Busy(p) || Carrying(p) ||
     std::clamp(RequestedMode.load(),0,2)==Original || !finiteVector(hit) || !finiteVector(momentum))return false;
  ensure(p->XLevel());
  if(auto it=puppets.find(p);it!=puppets.end() && it->second.age<.12f)return false;
  if(!puppets.contains(p) && puppets.size()>=12)return false;
  if(!makeRagdoll(p,vec3(0),nullptr,true,nullptr,false,true))return false;
  auto& s=puppets.at(p);int selected=-1;float nearest=1e30f;
  // A named/nearest upper-body branch reacts; legs stay with locomotion.
  // Kicks and shield hits can recoil the head without taking over navigation.
  for(size_t i=1;i<s.parts.size();++i) {
    auto& part=s.parts[i];auto name=s.model->Joints[part.joint].Name.ToString();
    if(name!="head" && name!="neck" && name.find("should")==std::string::npos && name.find("elbow")==std::string::npos)continue;
    float distance=part.joint==joint?-1.f:length(part.bindPoint-hit);
    if(distance<nearest){nearest=distance;selected=int(i);}
  }
  if(selected<0){clearPuppet(p);return false;}
  int rootJoint=s.parts[selected].joint;
  for(size_t i=1;i<s.parts.size();++i) {
    int j=s.parts[i].joint;bool child=false;
    for(int depth=0;j>0 && depth<64;++depth){if(j==rootJoint){child=true;break;}j=s.model->Joints[j].Parent;}
    if(child){s.activeJoints.insert(s.parts[i].joint);world->motion(s.parts[i].body,Motion::Dynamic);}
  }
  s.targetPose=s.rest;
  if(!DrivePuppet(s,1.f/60)){clearPuppet(p);return false;}
  auto direction=length(momentum)>1?normalize(momentum):vec3(-std::cos(p->Rotation().YawRadians()),-std::sin(p->Rotation().YawRadians()),.2f);
  auto& body=s.parts[selected];auto point=body.bindPoint;
  // A bounded off-centre impulse gives both rotation and deflection. The
  // gameplay momentum has already been applied by the authoritative hit.
  world->impulse(body.body,direction*std::clamp(p->Mass()/14.f,2.f,16.f)*190.f,point);
  if(commandline->HasArg("","--rune-puppet-trace") && commandline->HasArg("","--save-directory"))LogMessage("RUNE_PUPPET_REACT actor="+p->Name.ToString()+" replica="+std::to_string(p->NetworkReplica));
  return true;
}

bool FinishCorpse(UActor* body,vec3 velocity) {
  if(!body || !authority(body) || body->bHidden() || !body->IsA("Carcass"))return false;
  auto it=pendingDeaths.find(body);
  if(it==pendingDeaths.end())return makeRagdoll(body,velocity,nullptr,false,nullptr,true);
  auto seed=std::move(it->second);
  pendingDeaths.erase(it);
  seed.resting=false;
  return makeRagdoll(body,velocity,nullptr,false,&seed,true);
}
bool Impaled(UActor* corpse) {
  auto it=states.find(corpse);
  return it!=states.end() && it->second.impaler;
}

bool PinImpale(UPawn* holder,UActor* corpse) {
  auto it=states.find(corpse);
  if(!holder || !holder->Weapon() || holder->finisherHeld || !world || it==states.end())return false;
  auto& s=it->second;
  if(s.replica || s.living || s.prop || s.impaler)return false;
  auto* rig=model(holder);
  if(!humanoid(rig))return false;
  // Support just the chest. Gravity and all the existing joint constraints
  // continue to act on the pelvis, head, arms and legs from the death tick.
  for(const char* name:{"chest","spinec","torso","base_torso","pelvis"}){
    for(size_t i=0;i<s.parts.size();++i){
      auto& part=s.parts[i];
      if(s.model->Joints[part.joint].Name!=name)continue;
      s.impaler=holder;s.pinnedPart=int(i);
      s.pinWeapon=holder->Weapon();s.pinOffhand=holder->Dual.offhand;
      s.pinOrigin=s.pinLastHolder=holder->Location();s.pinYaw=holder->Rotation().YawRadians();
      s.pinTransform=world->get(part.body);s.pinAim=holder->finisherAimWorld;
      holder->finisherHeld=true;holder->finisherWithdraw=-1;
      world->motion(part.body,Motion::Kinematic);
      return true;
    }
  }
  return false;
}

bool ReleaseImpale(UPawn* holder) {
  if(!holder || !holder->finisherHeld || holder->NetworkReplica)return false;
  for(auto& [actor,s]:states)if(s.impaler==holder){unpin(s);return true;}
  return false;
}

void driveImpale(State& s,float dt){
  auto* p=s.impaler;
  auto movement=p->Location()-s.pinLastHolder;
  s.pinLastHolder=p->Location();
  float heading=p->Rotation().YawRadians();
  vec3 forward{std::cos(heading),std::sin(heading),0};
  s.pullback=std::max(0.f,s.pullback-dot(movement,forward));
  s.sidestep+=dot(movement,vec3(-forward.y,forward.x,0));
  if(!live(p) || p->bHidden() || p->Weapon()!=s.pinWeapon || p->Dual.offhand!=s.pinOffhand ||
     Controls(p) || std::hypot(s.pullback,s.sidestep)>10.f || std::abs(movement.z)>14.f || length(movement)>90.f || !RuneBattleLaws::Allowed(RuneBattleLaws::Finishers)){
    unpin(s);return;
  }
  // A campaign enemy elects to withdraw after showing the hit; multiplayer
  // bots send a normal Attack command through the same authority as players.
  if(!p->IsA("RunePlayer") && s.age>.85f){unpin(s);return;}
  // Keep the game's ordinary corpse cleanup clocks for after withdrawal.
  if(s.actor->LifeSpan()>0){s.pinLifeSpan=s.actor->LifeSpan();s.actor->LifeSpan()=0;}
  if(s.actor->TimerRate()>0){s.pinTimerRate=s.actor->TimerRate();s.actor->TimerRate()=0;}
  float angle=p->Rotation().YawRadians()-s.pinYaw;
  Rotation yaw{0,0,std::sin(angle*.5f),std::cos(angle*.5f)};
  float rise=std::clamp((s.age-.055f)/.19f,0.f,1.f);
  vec3 origin=p->Location()+vec3(0,0,12.f*rise*rise*(3.f-2.f*rise));
  auto at=origin+rotate(yaw,s.pinTransform.position-s.pinOrigin);
  // Let the torso tip over the support while the other rigid bodies settle.
  float tilt=.14f*rise;
  Rotation sag{-std::sin(heading)*std::sin(tilt),std::cos(heading)*std::sin(tilt),0,std::cos(tilt)};
  auto turn=multiply(sag,yaw);
  p->finisherAimWorld=at+rotate(turn,s.pinAim-s.pinTransform.position);
  world->kinematic(s.parts[s.pinnedPart].body,at,multiply(turn,s.pinTransform.rotation),dt);
}
bool Ragdoll(UActor *a, vec3 velocity, UPawn *instigator, bool living) {
  return makeRagdoll(a, velocity, instigator, living, nullptr);
}
void AfterScriptCall(UFunction *f, UObject *object,
                     const ArrayView<ExpressionValue> &args) {
  if (!applied || !engine || !engine->LaunchInfo.IsRune() ||
      f->Name != "InitFor" || args.size() < 1)
    return;
  auto *body = UObject::TryCast<UActor>(object);
  auto *source = UObject::TryCast<UPawn>(args[0].ToObject());
  if (!body || !authority(body) || !body->IsA("RuneCarcass") || !source ||
      source->Health() > 0 || source->bDeleteMe() || source->bHidden() ||
      source->XLevel() != body->XLevel() ||
      length(source->Location() - body->Location()) > 5)
    return;
  auto *m = model(source);
  if (!humanoid(m) || pendingDeaths.size() >= 64)
    return;
  CurrentScope current;
  DeathSeed seed;
  seed.model = m;
  seed.pose = m->ActorPose(source);
  seed.world = GetRuneMeshToWorld(source);
  seed.location = source->Location();
  seed.rotation = source->Rotation();
  seed.prePivot = source->PrePivot();
  seed.velocity = source->Physics() == PHYS_None ? vec3(0) : source->Velocity();
  seed.resting = source->Physics() == PHYS_None &&
                 !(source->ActorBase() && GetMovingBrush(source->ActorBase()));
  for (auto &j : seed.pose)
    for (int k = 0; k < 3; ++k)
      if (!std::isfinite(j.Position[k]) || !std::isfinite(j.Scale[k]) ||
          j.Scale[k] <= 0 || j.Scale[k] > 4)
        return;
  // The outer InitFor overwrites any nested Super call. Process at the end
  // of the level tick, after original attachment/base/death scripts finish.
  pendingDeaths[body] = std::move(seed);
}
static void adoptDeaths(ULevel *level) {
  auto pending = std::move(pendingDeaths);
  pendingDeaths.clear();
  for (auto &[body, seed] : pending) {
    if (!authority(body) || body->bHidden() || body->GetBool("bDecorative") ||
        body->XLevel() != level || Controls(body) || model(body) != seed.model)
      continue;
    bool probe = commandline->HasArg("", "--rune-physics-death-probe");
    if (commandline->HasArg("", "--death-handoff-baseline")) {
      if (probe)
        deathObservations[body] = {seed, 0, false};
      continue;
    }
    position(body, seed.location);
    body->SetRotation(seed.rotation);
    body->PrePivot() = seed.prePivot;
    if (!makeRagdoll(body, seed.velocity, nullptr, false, &seed))
      continue;
    float error = 0;
    if (probe) {
      auto a = seed.model->JointMatrices(seed.pose),
           b = seed.model->ActorJointMatrices(body);
      auto worldNow = GetRuneMeshToWorld(body);
      for (size_t j = 0; j < a.size(); ++j)
        error = std::max(
            error, length(point(seed.world * a[j]) - point(worldNow * b[j])));
      deathObservations[body] = {seed, error, seed.resting};
    }
    LogMessage("RUNE_PHYSICS_DEATH_HANDOFF actor=" + body->Name.ToString() +
               " sleeping=" + std::to_string(seed.resting));
  }
}
bool Prop(UActor *a, vec3 velocity, UPawn *instigator) {
  if (!RuneBattleLaws::Allowed(RuneBattleLaws::Props) || !applied || !authority(a) || Controls(a) || !SafeProp(a))
    return false;
  ensure(a->XLevel());
  if (std::count_if(states.begin(), states.end(), [](auto &item) {
        return !item.second.replica && item.second.prop;
      }) >= 16)
    return false;
  State s;
  s.actor = a;
  s.prop = true;
  s.saved = save(a);
  s.bindWorld = GetRuneMeshToWorld(a);
  s.bindLocation = a->Location();
  s.actorOrigin = a->Location();
  s.instigator = instigator;
  s.impactArmed = instigator && length(velocity) > 300;
  s.originalOwner = a->Owner();
  Description d;
  d.position = a->Location();
  d.halfExtent = {std::clamp(a->CollisionRadius(), 4.f, 30.f),
                  std::clamp(a->CollisionRadius(), 4.f, 30.f),
                  std::clamp(a->CollisionHeight(), 4.f, 35.f)};
  d.rotation = rotationOf(Coords::Rotation(a->Rotation()).ToMatrix());
  d.mass = std::clamp(a->Mass(), 5.f, 120.f);
  d.owner = uint64_t(uintptr_t(a));
  d.velocity = velocity;
  d.restitution = applied == Chaos ? .35f : .12f;
  if (auto *m = model(a)) {
    auto box = m->PoseBounds(a).transform(s.bindWorld);
    d.position = (box.min + box.max) * .5f;
    d.halfExtent = (box.max - box.min) * .5f;
    for (int i = 0; i < 3; ++i)
      d.halfExtent[i] = std::clamp(d.halfExtent[i], 2.f, 65.f);
    d.rotation = {};
    if (a->IsA("Keg") || a->IsA("Bucket")) {
      int axis = 0;
      for (int k = 1; k < 3; ++k)
        if (d.halfExtent[k] > d.halfExtent[axis])
          axis = k;
      vec3 along(0);
      along[axis] = 1;
      d.cylinder = true;
      d.halfHeight = d.halfExtent[axis];
      d.radius =
          std::max(d.halfExtent[(axis + 1) % 3], d.halfExtent[(axis + 2) % 3]);
      d.rotation = between({0, 1, 0}, along);
    }
  }
  s.bindLocation = d.position;
  s.carryHeight = d.halfExtent.z;
  auto body = world->create(d);
  if (body == Invalid)
    return false;
  world->angularVelocity(body, {1.2f, .4f, .7f});
  s.parts.push_back({body, 0, -1, d.rotation, {}, d.position, d.position});
  s.parts.back().current=s.parts.back().lastValid=world->get(body);
  a->SetPhysics(PHYS_None);
  a->bCollideWorld() = false;
  a->SetCollision(true, false, false);
  s.previousBody = s.currentBody = world->get(body);
  states.emplace(a, std::move(s));
  return true;
}
static bool BeginKnockdown(UPawn *p, UPawn *instigator, vec3 velocity, float strength, bool combatOnly) {
  if (!RuneBattleLaws::Allowed(RuneBattleLaws::Knockdown) || (!applied && !combatOnly) || !eligible(p) || Controls(p) ||
      (!p->IsA("RunePlayer") && !p->Enemy()) || RuneCorpse::Busy(p) ||
      (!combatOnly && resistantUntil[p] > simulationTime))
    return false;
  RuneSlam::Cancel(p);
  p->Bloodlust.phase = 0;
  p->RuneKickActive = p->RuneKickPending = false;
  p->RuneBashPhase = 0;
  p->RuneBashPending = false;
  p->SetObject("UseActor", nullptr);
  p->Dual.guard = false;
  p->Dual.guardBlend = 0;
  p->Tactics.attacking = p->Tactics.blocking = false;
  p->SetBool("bSwingingHigh", false);
  p->SetBool("bSwingingLow", false);
  for (auto *weapon : {p->Weapon(), p->Dual.offhand})
    if (weapon && !weapon->bDeleteMe()) {
      CallEvent(weapon, "FinishAttack");
      CallEvent(weapon, "DisableSwipeTrail");
    }
  CallEvent(p, "ActivateShield", {ExpressionValue::BoolValue(false)});
  CallEvent(p, "PlayUninterruptedAnim",
            {ExpressionValue::NameValue("neutral_idle")});
  return makeRagdoll(p, velocity * (applied == Chaos ? 1.6f : 1.f) * strength,
                 instigator, true, nullptr, combatOnly);
}
bool Knockdown(UPawn *p, UPawn *instigator, vec3 velocity, float strength) {
  return BeginKnockdown(p,instigator,velocity,strength,false);
}
bool ShieldRushKnockdown(UPawn *p, UPawn *instigator, vec3 velocity) {
  return BeginKnockdown(p,instigator,velocity,1.f,true);
}
void Impact(UPawn *attacker, UPawn *target, vec3 velocity, bool blocked) {
  if (!applied || blocked || !target)
    return;
  Knockdown(target, attacker, velocity);
}
void Release(UActor *a) { erase(a, true); }
void OnDestroy(UActor *a) {
  clearPuppet(a);
  RuneIK::Forget(a);
  pendingDeaths.erase(a);
  deathObservations.erase(a);
  hazards.erase(a);
  retiredBodies.erase(a);
  for (auto &[item, s] : states) {
    if (s.impaler == a || (s.impaler && (s.pinWeapon == a || s.pinOffhand == a)))unpin(s);
    if (s.instigator == a)
      s.instigator = nullptr;
    if (s.originalOwner == a)
      s.originalOwner = nullptr;
  }
  for (auto &[item, s] : states)
    if (s.carrier == a) {
      s.carrier = nullptr;
      if (world)
        world->motion(s.parts[0].body, Motion::Dynamic);
    }
  erase(a, false);
  if (world)
    if (auto it = movers.find(a); it != movers.end()) {
      world->remove(it->second);
      movers.erase(it);
    }
  if (world)
    if (auto it = proxies.find(a); it != proxies.end()) {
      world->remove(it->second);
      proxies.erase(it);
    }
  if (auto *p = UObject::TryCast<UPawn>(a))
    resistantUntil.erase(p);
}
void BeforeSave() {
  while(!puppets.empty())clearPuppet(puppets.begin()->first);
  std::vector<UActor *> all;
  for (auto &[a, s] : states)
    all.push_back(a);
  for (auto *a : all)
    erase(a, true);
}
void Tick(ULevel *level, float elapsed, bool paused) {
  if (!engine || !engine->LaunchInfo.IsRune() || level != engine->Level)
    return;
  bool remote=false;
#ifdef OPENRUNE_ENABLE_NETWORK
  remote=engine->NetworkClient;
#endif
  if(remote) {
    if(paused || elapsed<=0)return;
    if(!puppetEnabled() || RequestedMode.load()==Original){while(!puppets.empty())clearPuppet(puppets.begin()->first);return;}
    ensure(level); // Prewarm scenery at join, rather than on the first hit.
    if(puppets.empty())return;
    accumulator=std::min(.15f,accumulator+elapsed);
    while(accumulator>=1.f/60) {accumulator-=1.f/60;StepPuppets(1.f/60,true);world->step(1.f/60);StepPuppets(1.f/60,false);}
    return;
  }
  int requested = std::clamp(RequestedMode.load(), 0, 2);
  if (requested != applied) {
    BeforeSave();
    Reset();
    applied = requested;
    LogMessage("RUNE_PHYSICS_MODE mode=" + std::to_string(applied));
  }
  if (paused || elapsed <= 0 || engine->LevelInfo->bPlayersOnly())
    return;
  if(!applied && states.empty() && puppets.empty()){simulationTime+=elapsed;return;}
  ensure(level);
  if(applied)adoptDeaths(level);
  simulationTime += elapsed;
  scan -= elapsed;
  if (applied && scan <= 0) {
    scan = .25f;
    std::vector<UActor *> bodies;
    for (auto *a : level->Actors)
      if (a && !a->bDeleteMe() && !a->bHidden() && !Controls(a) &&
          !retiredBodies.contains(a) && authority(a) && a->IsA("RuneCarcass") &&
          !a->GetBool("bDecorative") && !a->bCarriedItem() &&
          !a->CorpseBody.active)
        bodies.push_back(a);
    for (auto *a : bodies)
      Ragdoll(a, a->Velocity());
    std::vector<UActor *> loose;
    for (auto *a : level->Actors)
      if (!Controls(a) && authority(a) && SafeProp(a))
        loose.push_back(a);
    for (auto *a : loose) {
      bool nearbyPawn = false;
      for (auto *other : level->Actors)
        if (live(other) && !other->NetworkReplica &&
            length(other->Location() - a->Location()) < 220) {
          nearbyPawn = true;
          break;
        }
      if (nearbyPawn)
        Prop(a, vec3(0));
    }
  }
  accumulator = std::min(.15f, accumulator + elapsed);
  auto start = std::chrono::steady_clock::now();
  while (accumulator >= 1.f / 60) {
    constexpr float dt = 1.f / 60;
    accumulator -= dt;
    for (auto *a : level->Actors)
      if (auto *p = UObject::TryCast<UPawn>(a); p && authority(p) &&
                                                p->Health() > 0 &&
                                                !Controls(p) && !p->bHidden()) {
        auto it = proxies.find(p);
        auto r = between({0, 1, 0}, {0, 0, 1});
        if (it == proxies.end()) {
          Description d;
          d.position = p->Location();
          d.capsule = true;
          d.radius = p->CollisionRadius();
          d.halfHeight = std::max(1.f, p->CollisionHeight() - d.radius);
          d.rotation = r;
          d.owner = uint64_t(uintptr_t(p));
          d.motion = Motion::Kinematic;
          auto h = world->create(d);
          if (h != Invalid)
            proxies[p] = h;
        } else
          world->kinematic(it->second, p->Location(), r, dt);
      }
    for (auto it = proxies.begin(); it != proxies.end();)
      if (!it->first || it->first->bDeleteMe() || !live(it->first) ||
          Controls(it->first)) {
        world->remove(it->second);
        it = proxies.erase(it);
      } else
        ++it;
    for (auto *a : level->Actors)
      if (auto *brush = GetMovingBrush(a);
          brush && !brush->bDeleteMe() &&
          (brush->bBlockPlayers() || brush->bBlockActors()) && brush->Brush()) {
        auto transform = GetMovingBrushTransform(brush);
        auto it = movers.find(a);
        if (it == movers.end()) {
          auto triangles = geometry(brush->Brush());
          auto rotation = rotationOf(transform);
          auto scale = brush->MainScale().Scale;
          for (auto &t : triangles)
            for (auto *p : {&t.a, &t.b, &t.c})
              *p = (*p - brush->PrePivot()) * scale;
          auto h = world->mesh(triangles, brush->Location(), rotation,
                               uint64_t(uintptr_t(brush)));
          if (h != Invalid)
            movers[a] = h;
        } else
          world->kinematic(it->second, brush->Location(), rotationOf(transform),
                           dt);
      }
    for (auto &[a, s] : states)
      if (!s.replica && s.carrier) {
        auto *p = s.carrier;
        if (!live(p) || p->Physics() != PHYS_Walking) {
          s.carrier = nullptr;
          s.release = -1;
          s.impactArmed = false;
          s.instigator = nullptr;
          s.victims.clear();
          a->SetOwner(s.originalOwner);
          world->motion(s.parts[0].body, Motion::Dynamic);
          continue;
        }
        p->bFire() = p->bAltFire() = 0;
        auto f = vec3(std::cos(p->Rotation().YawRadians()),
                      std::sin(p->Rotation().YawRadians()), 0);
        auto at = p->Location() +
                  vec3(0, 0, p->CollisionHeight() + 11 + s.carryHeight) +
                  f * 12.f;
        vec3 hit, normal;
        if (p->Trace(hit, normal, at, p->Location(), false, vec3(12)))
          at = hit + normal * 14.f;
        world->kinematic(s.parts[0].body, at, {}, dt);
        if (s.release >= 0) {
          s.release -= dt;
          if (s.release <= 0) {
            s.carrier = nullptr;
            s.release = -1;
            a->SetOwner(s.originalOwner);
            s.victims.clear();
            s.impactArmed = true;
            s.instigator = p;
            world->motion(s.parts[0].body, Motion::Dynamic);
            world->ignore(uint64_t(uintptr_t(a)), uint64_t(uintptr_t(p)), .35f);
            world->velocity(s.parts[0].body, f * 650.f + vec3(0, 0, 140));
            world->angularVelocity(s.parts[0].body, {3, 2, 1});
            LogMessage("RUNE_PHYSICS_PROP_THROW");
          }
        }
      }
    for (auto &[a, s] : states)
      if (!s.replica) {
        s.previousPose = s.pose;
        s.previousLocation = a->Location();
        s.previousBody = s.currentBody;
      }
    for(auto& [a,s]:states)if(!s.replica && s.recovering && puppetEnabled())DriveRecovery(s,dt);
    for(auto& [actor,s]:states)if(s.impaler)driveImpale(s,dt);
    StepPuppets(dt,true);
    world->step(dt);
    StepPuppets(dt,false);
    // Validate the actual solver results before contact impulses or snapshots
    // can spread a bad body state to another actor. Normal poses reuse these
    // reads, so this adds no second per-bone physics query in ordinary play.
    std::vector<UActor*> invalidMotion;
    for(auto& [actor,state]:states)if(!state.replica&&!readMotion(state))invalidMotion.push_back(actor);
    for(auto* actor:invalidMotion){
      actor->Velocity()=vec3(0);erase(actor,true);
      if(actor->IsA("RuneCarcass")){actor->SetPhysics(PHYS_None);retiredBodies.insert(actor);}
      LogMessage("RUNE_PHYSICS_MOTION_RETIRED actor="+actor->Name.ToString());
    }
    contacts();
    for (auto &[obstacle, h] : hazards)
      if (!obstacle->bDeleteMe()) {
        std::vector<UPawn *> hit;
        for (auto *a : level->Actors)
          if (auto *p = UObject::TryCast<UPawn>(a);
              p && eligible(p) && h.cooldown[p] <= simulationTime) {
            auto d = p->Location() - obstacle->Location();
            float range = std::clamp(obstacle->CollisionRadius(), 10.f, 45.f) +
                          p->CollisionRadius();
            if (length(d.xy()) < range &&
                std::abs(d.z) <
                    p->CollisionHeight() +
                        std::max(10.f, obstacle->CollisionHeight()) &&
                (h.kind == 2 || length(p->Velocity()) > 180))
              hit.push_back(p);
          }
        for (auto *p : hit) {
          h.cooldown[p] = simulationTime + .75;
          CallEvent(p, "JointDamaged",
                    {ExpressionValue::IntValue(h.kind == 2 ? 8 : 25),
                     ExpressionValue::ObjectValue(nullptr),
                     ExpressionValue::VectorValue(p->Location()),
                     ExpressionValue::VectorValue(vec3(0)),
                     ExpressionValue::NameValue(h.kind == 2 ? "fire" : "sever"),
                     ExpressionValue::IntValue(0)});
        }
      }

    std::vector<UActor *> finish;
    for (auto &[a, s] : states) {
      if (s.replica)
        continue;
      s.age += dt;
      if (a->bDeleteMe()) {
        finish.push_back(a);
        continue;
      }
      auto t = s.parts[0].current;
      if (s.prop) {
        s.currentBody = t;
        position(a, t.position);
        a->Velocity() = t.velocity;
      } else
        pose(s,true);
      if (s.living) {
        if (!live(a)) {
          finish.push_back(a);
          continue;
        }
        if (length(t.velocity) < 70)
          s.still += dt;
        else
          s.still = 0;
        if (!s.recovering && s.age > 1 && s.age >= s.stunUntil &&
            (s.still > .25f || s.age > 3)) {
          vec3 hit, normal;
          auto center = a->Location();
          if (a->Trace(hit, normal, center - vec3(0, 0, 160),
                       center + vec3(0, 0, 35), false, vec3(0)) &&
              normal.z > .7f) {
            auto at = hit + vec3(0, 0, s.saved.height + 1);
            auto candidate =
                a->CheckLocation(at, s.saved.radius, s.saved.height, true);
            if (candidate.first) {
              s.recovering = true;
              s.recoveryLocation = candidate.second;
              s.recoveryStart = a->Location();
              s.recoveryBody=s.parts[0].current;
              if(puppetEnabled())world->motion(s.parts[0].body,Motion::Kinematic);
              else for(auto& part:s.parts)world->sleep(part.body);
            }
          }
        }
        if (s.recovering) {
          s.recovery += dt;
          pose(s);
          if (s.recovery >= (puppetEnabled()?.6f:.45f))
            finish.push_back(a);
        }
        if (s.age > 6)
          finish.push_back(a);
      }
    }
    for (auto *a : finish)
      erase(a, !a->bDeleteMe());
  }
  counters = {};
  for (auto &[a, s] : states)
    if (s.prop)
      ++counters.props;
    else
      ++counters.ragdolls;
  counters.wallSlams = wallSlams;
  counters.brokenProps = brokenProps;
  counters.awake = world->activeBodies();
  counters.bodies = world->bodies();
  counters.stepMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
}
bool UseProp(UPawn *p) {
  if (!applied || !authority(p))
    return false;
  for (auto &[a, s] : states)
    if (s.carrier == p) {
      s.carrier = nullptr;
      s.release = -1;
      s.impactArmed = false;
      s.instigator = nullptr;
      s.victims.clear();
      a->SetOwner(s.originalOwner);
      world->motion(s.parts[0].body, Motion::Dynamic);
      world->velocity(s.parts[0].body, vec3(0));
      return true;
    }
  if (!RuneBattleLaws::Allowed(RuneBattleLaws::Props) || !live(p) || Controls(p) || RuneCorpse::Busy(p) ||
      !RuneCorpse::Eligible(p) || p->Physics() != PHYS_Walking)
    return false;
  UActor *nearest = nullptr;
  float distance = 105;
  auto f = vec3(std::cos(p->Rotation().YawRadians()),
                std::sin(p->Rotation().YawRadians()), 0);
  for (auto &[a, s] : states)
    if (s.prop && !s.carrier && !a->bHidden()) {
      auto d = a->Location() - p->Location();
      auto n = length(d);
      if (n < distance && dot(d, f) > 0 &&
          p->FastTrace(a->Location(), p->Location())) {
        nearest = a;
        distance = n;
      }
    }
  if (!nearest)
    return false;
  for (auto *a : p->XLevel()->Actors)
    if (a && !a->NetworkReplica && RuneCorpse::Throwable(a) &&
        length(a->Location() - p->Location()) < distance &&
        p->FastTrace(a->Location(), p->Location()))
      return false;

  for (auto *a : p->XLevel()->Actors)
    if (a && !a->bDeleteMe() && a != nearest && !a->NetworkReplica &&
        (!a->bHidden() || !a->IsA("Inventory")) && !IsProp(a) &&
        !a->IsA("Carcass") && length(a->Location() - p->Location()) < 100 &&
        CallEvent(a, "CanBeUsed", {ExpressionValue::ObjectValue(p)}).ToBool())
      return false;
  RuneDualWield::Unequip(p);
  CallEvent(p, "ActivateShield", {ExpressionValue::BoolValue(false)});
  if (p->Weapon())
    CallEvent(p->Weapon(), "FinishAttack");
  p->bFire() = p->bAltFire() = 0;
  auto &s = states.at(nearest);
  s.carrier = p;
  s.instigator = nullptr;
  s.impactArmed = false;
  s.victims.clear();
  s.release = -1;
  s.victims.clear();
  p->CorpseCarry.age = 0;
  nearest->SetOwner(p);
  world->motion(s.parts[0].body, Motion::Kinematic);
  return true;
}
bool ThrowProp(UPawn *p) {
  if(!RuneBattleLaws::Allowed(RuneBattleLaws::Props)&&Carrying(p)){UseProp(p);return true;}
  for (auto &[a, s] : states)
    if (s.carrier == p) {
      if (s.release < 0) {
        s.release = .28f;
        p->PlayAnim("ATK_ALL_throw1_AA0S", 1, .06f);
      }
      return true;
    }
  return false;
}

bool FindImpactLane(UPlayerPawn *p, vec3 &lane, vec3 &dir, vec3 &wall) {
  bool found = false;
  auto actors = p->XLevel()->Actors;
  for (auto *spot : actors)
    if (spot && spot->IsA("PlayerStart") && !found)
      for (int i = 0; i < 32 && !found; ++i) {
        float angle = i * 3.14159265f / 16;
        vec3 d(std::cos(angle), std::sin(angle), 0), hit, normal;
        auto start = spot->Location();
        if (!p->Trace(hit, normal, start + d * 360.f, start, false, vec3(0)) ||
            std::abs(normal.z) > .3f || dot(normal, d) > -.8f ||
            length(hit - start) < 130)
          continue;
        auto at = hit - d * 65.f;
        vec3 floor, n;
        if (!p->Trace(floor, n, at - vec3(0, 0, 160), at + vec3(0, 0, 40),
                      false, vec3(0)) ||
            n.z < .7f)
          continue;
        at = floor + vec3(0, 0, 46);
        if (!p->CheckLocation(at, 24, 46, true).first ||
            !p->CheckLocation(at - d * 110.f, 24, 46, true).first)
          continue;
        lane = at;
        dir = d;
        wall = hit;
        found = true;
      }
  return found;
}
bool KickObject(UPawn *p) {
  if (!applied || !authority(p) || !live(p) || Carrying(p))
    return false;
  float yaw = p->Rotation().YawRadians();
  vec3 forward(std::cos(yaw), std::sin(yaw), 0);
  UActor *nearest = nullptr;
  float distance = 130;
  for (auto &[a, s] : states) {
    if ((s.prop&&!RuneBattleLaws::Allowed(RuneBattleLaws::Props)) || (!s.prop&&!RuneBattleLaws::Allowed(RuneBattleLaws::Corpse)) || s.replica || s.living || s.carrier || a->bDeleteMe() || a->bHidden() ||
        (!s.prop && !a->IsA("Carcass")))
      continue;
    auto at = s.prop ? world->get(s.parts[0].body).position
                     : RuneCorpse::InteractionPoint(a);
    auto delta = at - p->Location();
    float vertical = std::abs(delta.z);
    delta.z = 0;
    float reach = length(delta);
    if (reach < 1 || reach > p->CollisionRadius() + a->CollisionRadius() + 50 ||
        reach >= distance || vertical > p->CollisionHeight() + 30 ||
        dot(delta / reach, forward) < .65f || !p->FastTrace(at, p->Location()))
      continue;
    nearest = a;
    distance = reach;
  }
  if (!nearest)
    return false;
  auto &s = states.at(nearest);
  s.instigator = p;
  s.impactArmed = true;
  s.victims.clear();
  float force = s.prop ? 650.f : 520.f;
  force *= applied == Chaos ? 1.35f : 1.f;
  auto velocity = forward * force + vec3(0, 0, 180);
  for (auto &part : s.parts) {
    world->wake(part.body);
    world->velocity(part.body, velocity);
    world->angularVelocity(part.body, vec3(forward.y, -forward.x, .2f) * 4.f);
  }
  world->ignore(uint64_t(uintptr_t(nearest)), uint64_t(uintptr_t(p)), .35f);
  if (p->HasProperty("LandSoundFlesh"))
    if (auto *sound =
            p->GetUObject(s.prop ? "LandSoundWood" : "LandSoundFlesh"))
      CallEvent(nearest, "PlaySound",
                {ExpressionValue::ObjectValue(sound),
                 ExpressionValue::IntValue(1),
                 ExpressionValue::FloatValue(.9f)});
  RuneFeedback::Impact(p, nullptr, false, false);
  LogMessage("RUNE_PHYSICS_KICK_OBJECT actor=" + nearest->Name.ToString());
  return true;
}
bool ScriptCall(UFunction *f, UObject *object,
                const ArrayView<ExpressionValue> &, ExpressionValue &result) {
  if (!engine || !engine->LaunchInfo.IsRune())
    return false;
  auto *a = UObject::TryCast<UActor>(object);
  // Carcass death-state/timer scripts still need to finish and permit gibbing.
  // Only their original ground-alignment functions yield to articulated pose.
  if (a && Controls(a) && a->IsA("Carcass") &&
      (f->Name == "UpdateCollisionCyllinder" || f->Name == "UpdateRotation" ||
       f->Name == "BaseChange"))
    return true;
  auto *p = UObject::TryCast<UPawn>(object);
  if (!p)
    return false;
  if (authority(p) &&
      ((f->Name == "Use" && UseProp(p)) ||
       ((f->Name == "Fire" || f->Name == "Throw") && ThrowProp(p)))) {
    result = ExpressionValue::BoolValue(true);
    return true;
  }
  if (!Controls(p))
    return false;
  for (auto name :
       {"Fire", "Attack", "AltFire", "Jump", "Use", "Throw", "GrabEdge",
        "SwitchWeapon", "Powerup", "ActivateShield", "PlayerMove",
        "ProcessMove", "AnimEnd", "UseNotify", "WeaponActivate",
        "SwipeEffectStart", "DropWeapon", "ThrowWeapon", "Defend"})
    if (f->Name == name) {
      result = ExpressionValue::BoolValue(false);
      return true;
    }
  return false;
}
Counters Stats() { return counters; }
std::string Status() {
  return "Ragdolls " + std::to_string(counters.ragdolls) + " · Props " +
         std::to_string(counters.props) + " · Awake " +
         std::to_string(counters.awake);
}
void RunProbe(Engine &game) {
  unsigned checks = 0;
  auto check = [&](bool ok, const char *text) {
    ++checks;
    if (!ok)
      throw std::runtime_error(std::string("cinematic physics: ") + text);
  };
  if (commandline->HasArg("", "--rune-physics-save-probe")) {
    check(commandline->HasArg("", "--save-directory"),
          "isolated save directory");
    auto url = game.GetDefaultURL("DM-Hildir.run");
    url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");
    game.LoadMap(url);
    game.LoginPlayer();
    auto *p = game.viewport->Actor();
    p->Health() = 1000;
    p->DisableEvent("PlayerTick");
    RequestedMode = Cinematic;
    auto step = [&](int n) {
      for (int i = 0; i < n; ++i) {
        game.LevelInfo->TimeSeconds() += 1.f / 120;
        game.TotalTime += 1.f / 120;
        game.Level->Tick(1.f / 120, false);
      }
    };
    step(80);
    check(Knockdown(p, nullptr, {120, 0, 200}), "save during knockdown");
    step(30);
    check(Controls(p), "still physical before save");
    game.SaveGameToSlot(90, "Cinematic physics isolated validation");
    check(states.empty(), "save restores script ownership");
    check(fs::is_regular_file(game.packages->GetSaveFolderPath() /
                              ("Save90." + game.packages->GetSaveExtension())),
          "real save file written");
    game.LoadFromSaveFile(UnrealURL("?load=90"));
    game.PossessSavedPlayer();
    RequestedMode = Original;
    step(120);
    p = game.viewport->Actor();
    check(p && p->Health() > 0 && !Controls(p) &&
              p->GetStateName() == "PlayerWalking",
          "saved player resumes alive and controllable");
    std::cout << "RUNE_PHYSICS_SAVE_OK checks=" << checks << "\n";
    return;
  }
#ifndef OPENRUNE_ENABLE_NETWORK
  throw std::runtime_error("Pose replication probe requires networking; "
                           "gameplay physics remains available.");
#else
  auto url = game.GetDefaultURL(
      commandline->GetArg("", "--probe-map", "DM-Hildir.run"));
  bool campaign = commandline->HasArg("", "--rune-physics-campaign");
  game.NetworkAuthority = !campaign;
  url.AddOrReplaceOption(campaign ? "game=RuneI.RuneSinglePlayer"
                                  : "game=RuneI.RuneMultiPlayer");
  game.LoadMap(url);
  UPlayerPawn *p = nullptr;
  if (campaign) {
    game.LoginPlayer();
    p = game.viewport->Actor();
  } else
    p = game.LoginNetworkPlayer("Physics");
  check(p, "spawn physics player");
  game.viewport->Actor() = p;
  p->Health() = 1000;
  p->DisableEvent("PlayerTick");
  RequestedMode =
      commandline->HasArg("", "--rune-physics-death-original")
          ? 0
          : std::clamp(commandline->GetArgInt("", "--rune-physics", Cinematic),
                       commandline->HasArg("", "--rune-physics-kick-probe") ? 0 : 1, 2);
  auto tick = [&](int n) {
    for (int i = 0; i < n; ++i) {
      game.LevelInfo->TimeSeconds() += 1.f / 120;
      game.TotalTime += 1.f / 120;
      game.Level->Tick(1.f / 120, false);
    }
  };
  tick(60);
  if(commandline->HasArg("","--rune-ceiling-probe")){RunRuneCeilingProbe(game,p);return;}
  if(commandline->HasArg("","--rune-puppet-probe")) {
    unsigned avatars=0,reacted=0;float maxReaction=0,maxStep=0;
    auto advance=[&](int n){for(int i=0;i<n;++i){game.LevelInfo->TimeSeconds()+=1.f/120;game.TotalTime+=1.f/120;Tick(game.Level,1.f/120,false);maxStep=std::max(maxStep,float(Stats().stepMs));}};
    bool disabled=!puppetEnabled();
    for(size_t index=0;index<openrune::Characters.size();++index)for(int skin=0;skin<openrune::Characters[index].skins;++skin) {
      if(campaign && (index || skin))continue;
      auto* v=campaign?p:game.LoginNetworkPlayer("MuscleAvatar",-1,int(index),skin);
      check(v,"muscle avatar");v->DisableEvent("PlayerTick");v->DisableEvent("Tick");v->GotoState("","");v->Health()=1000;v->SetPhysics(PHYS_Walking);
      auto* m=model(v);check(m && humanoid(m),"humanoid puppet");auto at=v->Location();auto saved=save(v);auto velocity=v->Velocity();
      auto bones=m->JointMatrices(m->ActorPose(v));int head=0;for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name=="head")head=int(j);
      auto hit=point(GetRuneMeshToWorld(v)*bones[head]);bool began=React(v,hit,{600,120,100},head);
      check(began!=disabled,"muscle toggle");
      if(!disabled) {
        check(puppets.contains(v) && !Controls(v) && !CustomTransform(v),"cosmetic ownership only");
        check(!React(v,hit,{600,0,0},head),"duplicate damage hook bounded");
        openrune::net::PhysicalPose wire;check(!Capture(v,wire),"muscle not a full-body wire override");
        float reaction=0;
        for(int frame=0;frame<60;++frame) {
          advance(1);auto raw=m->ActorPose(v),presented=raw;
          {PresentationScope present;ApplyPose(v,presented);}
          for(size_t j=0;j<raw.size();++j){
            check(length(raw[j].Position-presented[j].Position)<1e-6f && length(raw[j].Scale-presented[j].Scale)<1e-6f,"no muscle bone stretch");
            auto a=rotationOf(RunePoseRotation(raw[j].Rotation)),b=rotationOf(RunePoseRotation(presented[j].Rotation));
            float dot=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);reaction=std::max(reaction,2*std::acos(std::clamp(dot,0.f,1.f)));
          }
        }
        check(reaction>.01f,"visible nonzero physical reaction");maxReaction=std::max(maxReaction,reaction);++reacted;
        check(length(v->Location()-at)<.001f && length(v->Velocity()-velocity)<.001f && v->Physics()==saved.physics && bool(v->bBlockActors())==saved.blockActors,"muscle preserves gameplay movement");
        advance(100);check(!puppets.contains(v),"reaction retires without actor restore");
      }
      ++avatars;if(!campaign)v->Destroy();
    }
    if(!disabled) {
      p->SetPhysics(PHYS_Walking);p->GotoState("","");p->Health()=1000;
      clearPuppet(p);int health=p->Health();
      CallEvent(p,"JointDamaged",{ExpressionValue::IntValue(5),ExpressionValue::ObjectValue(nullptr),ExpressionValue::VectorValue(p->Location()+vec3(0,0,20)),ExpressionValue::VectorValue(vec3(200,0,20)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
      check(p->Health()<health && puppets.contains(p),"actual committed damage triggers a muscle reaction");
      clearPuppet(p);p->Health()=1000;
      check(React(p,p->Location()+vec3(0,0,30),{400,40,0}),"fault-isolation puppet");
      auto previousVelocity=p->Velocity();p->Velocity()={15,25,0};
      puppets.at(p).parts[0].current.velocity.x=std::numeric_limits<float>::quiet_NaN();
      check(!validateMotion(puppets.at(p)) && p->Velocity()==vec3(15,25,0),"invalid cosmetic solve cannot change gameplay velocity");
      clearPuppet(p);p->Velocity()=previousVelocity;

      check(React(p,p->Location()+vec3(0,0,30),{400,40,0}),"pre-save puppet");
      auto at=p->Location();BeforeSave();check(puppets.empty() && length(p->Location()-at)<.001f,"save removes cosmetics without movement");
      check(React(p,p->Location()+vec3(0,0,30),{400,40,0}),"pre-teleport puppet");
      position(p,at+vec3(200,0,0));advance(2);check(!puppets.contains(p),"teleport invalidates old physical target");position(p,at);
      auto previousClient=game.NetworkClient;p->NetworkReplica=true;game.NetworkClient=true;
      check(React(p,p->Location()+vec3(0,0,30),{400,40,0}),"remote committed hit supports local muscle");
      advance(20);check(puppets.contains(p) && !Controls(p),"remote cosmetic ticks without movement authority");
      advance(100);check(!puppets.contains(p),"remote muscle retires");game.NetworkClient=previousClient;p->NetworkReplica=false;
      check(React(p,p->Location()+vec3(0,0,30),{400,40,0}),"pre-knockdown puppet");
      check(Knockdown(p,nullptr,{100,30,180}),"partial promotes to full physical knockdown");
      check(Controls(p) && !puppets.contains(p),"no double rig after knockdown");
      bool recovering=false,driving=false;for(int i=0;i<840 && Controls(p);++i){advance(1);if(auto it=states.find(p);it!=states.end()){recovering|=it->second.recovering;driving|=!it->second.targetPose.empty();}}
      check(recovering && driving && !Controls(p) && p->Health()>0,"motored standing recovery completes alive");
      check(world->bodies()<300,"transient muscle bodies released");
      if(!campaign) {
        std::vector<UPlayerPawn*> crowd;
        for(int i=0;i<13;++i){auto* bot=game.LoginNetworkPlayer("MuscleBudget");check(bot,"budget avatar");bot->Health()=1000;bot->SetPhysics(PHYS_Walking);bot->DisableEvent("PlayerTick");bot->DisableEvent("Tick");bot->GotoState("","");crowd.push_back(bot);
          Array<RuneJointPose> otherPrevious;if(i==1)otherPrevious=puppets.at(crowd[0]).previousPose;
          bool accepted=React(bot,bot->Location()+vec3(0,0,30),{300,0,50});check(accepted==(i<12),"bounded concurrent muscle budget");
          if(i==1){auto& after=puppets.at(crowd[0]).previousPose;check(after.size()==otherPrevious.size(),"other muscle interpolation retained");for(size_t j=0;j<after.size();++j)check(after[j].Rotation==otherPrevious[j].Rotation && after[j].Position==otherPrevious[j].Position,"new hit does not reset another character interpolation");}
          if(i==0)advance(12);
        }
        float peak=0;for(int i=0;i<100;++i){advance(1);peak=std::max(peak,float(Stats().stepMs));}
        check(puppets.empty(),"budget crowd retires all rigs");for(auto* bot:crowd)bot->Destroy();advance(2);
        std::cout<<"RUNE_PUPPET_STRESS_OK concurrent=12 max_step_ms="<<peak<<std::endl;
      }

    }
    std::cout<<"RUNE_PUPPET_RUNTIME_OK avatars="<<avatars<<" reacted="<<reacted<<" disabled="<<disabled<<" max_reaction_radians="<<maxReaction<<" max_step_ms="<<maxStep<<" checks="<<checks<<std::endl;return;
  }
  if(commandline->HasArg("","--rune-ik-probe")){RuneIK::RunProbe(game,p);return;}
  if(commandline->HasArg("","--rune-physics-recovery-probe")) {
    check(!campaign,"recovery fixture uses authority capture");
    auto* source=game.LoginNetworkPlayer("RecoverySource");source->DisableEvent("PlayerTick");source->DisableEvent("Tick");source->GotoState("","");
    check(source->SetLocation(p->Location()+vec3(0,0,150)),"recovery source placement");source->SetPhysics(PHYS_Falling);source->Velocity()={190,40,-110};source->Health()=0;
    auto* body=UObject::TryCast<UActor>(CallEvent(source,"SpawnCarcass").ToObject());check(body,"recovery carcass");adoptDeaths(game.Level);check(Controls(body),"physical corpse");source->Destroy();tick(8);
    RuneNetworkAuthority net(game);check(net.bind(1,p,1),"recovery capture owner");openrune::net::World captured;
    auto& state=states.at(body);auto count=world->bodies();auto at=body->Location();
    body->Velocity().x=std::numeric_limits<float>::quiet_NaN();
    check(net.capture(captured),"nonfinite corpse velocity repaired before snapshot");
    check(finiteVector(body->Velocity())&&length(body->Location()-at)<.001f&&Controls(body),"actor repair retains body and pose");
    for(int fault=0;fault<3;++fault) {
      readMotion(state);auto expected=state.parts[0].lastValid.position;
      if(fault==0)state.parts[0].current.velocity.x=std::numeric_limits<float>::quiet_NaN();
      else if(fault==1)state.parts.back().current.rotation.w=std::numeric_limits<float>::infinity();
      else state.parts.back().current.position.z=std::numeric_limits<float>::quiet_NaN();
      check(validateMotion(state),"corrupt solver result restored");pose(state,true);
      check(world->bodies()==count&&length(world->get(state.parts[0].body).position-expected)<.001f,"whole ragdoll restored without teleport/deletion");
      for(auto& part:state.parts)check(finiteMotion(world->get(part.body))&&length(world->get(part.body).velocity)<.001f,"repaired parts finite and at rest");
      check(net.capture(captured),"repaired corpse snapshot valid");
      world->velocity(state.parts[0].body,{100,20,80});tick(6);
    }
    auto old=p->Velocity();p->Velocity().x=std::numeric_limits<float>::quiet_NaN();
    check(!net.capture(captured),"unowned invalid pawn still rejected");p->Velocity()=old;
    auto location=body->Location();body->Location().x=std::numeric_limits<float>::quiet_NaN();
    check(!net.capture(captured),"invalid snapshot position still rejected");position(body,location);
    check(net.capture(captured),"world resumes after fault injection");
    tick(240);check(Controls(body)&&finiteVector(body->Location())&&finiteVector(body->Velocity()),"repaired ragdoll continues simulating");
    std::cout<<"RUNE_PHYSICS_RECOVERY_OK checks="<<checks<<" parts="<<state.parts.size()<<" bodies="<<world->bodies()<<std::endl;return;
  }
  if(commandline->HasArg("","--rune-physics-chaos-roster-probe")) {
    check(!campaign,"chaos roster fixture requires authority");
    RuneNetworkAuthority net(game);check(net.bind(1,p,1),"chaos fixture player bind");
    unsigned cases=0,snapshots=0;
    bool pile=commandline->HasArg("","--chaos-pile");std::vector<UActor*> piled;
    for(size_t character=0;character<openrune::Characters.size();++character)
      for(int skin=0;skin<openrune::Characters[character].skins;++skin) {
        auto* source=game.LoginNetworkPlayer("ChaosVictim",-1,int(character),skin);
        check(source,"roster death source");source->DisableEvent("PlayerTick");source->DisableEvent("Tick");source->GotoState("","");
        check(source->SetLocation(p->Location()+vec3(0,0,160)),"roster source placement");
        source->SetRotation(Rotator(0,int(character)*2711,0));source->SetPhysics(PHYS_Falling);
        source->Velocity()=vec3(800+skin*100,450,-220);source->Health()=0;
        auto* body=UObject::TryCast<UActor>(CallEvent(source,"SpawnCarcass").ToObject());check(body,"roster SpawnCarcass");
        adoptDeaths(game.Level);check(Controls(body),"roster handoff controlled");source->Destroy();
        std::cout<<"CHAOS_ROSTER_START class="<<openrune::Characters[character].className<<" skin="<<skin<<std::endl;
        for(int frame=0;frame<(pile?2:180);++frame) {
          tick(1);
          if(auto it=states.find(body);it!=states.end())for(auto& part:it->second.parts) {
            auto t=world->get(part.body);
            for(int k=0;k<3;++k)check(std::isfinite(t.position[k])&&std::isfinite(t.velocity[k])&&std::isfinite(t.angular[k]),"roster rigid state finite");
            check(std::isfinite(t.rotation.x)&&std::isfinite(t.rotation.y)&&std::isfinite(t.rotation.z)&&std::isfinite(t.rotation.w),"roster rigid rotation finite");
          }
          if(frame%10==0){openrune::net::World captured;check(net.capture(captured),"roster finite world capture");++snapshots;}
        }
        if(pile)piled.push_back(body);else if(!body->bDeleteMe())body->Destroy();++cases;
      }
    if(pile)for(int frame=0;frame<3600;++frame) {
      tick(1);
      for(auto& [actor,state]:states)if(!state.replica)for(auto& part:state.parts){auto t=world->get(part.body);for(int k=0;k<3;++k)check(std::isfinite(t.position[k])&&std::isfinite(t.velocity[k])&&std::isfinite(t.angular[k]),"pile rigid state finite");}
      if(frame%10==0){openrune::net::World captured;check(net.capture(captured),"pile finite snapshot");++snapshots;}
    }
    std::cout<<"CHAOS_ROSTER_OK cases="<<cases<<" snapshots="<<snapshots<<" checks="<<checks<<std::endl;return;
  }
  if(commandline->HasArg("", "--rune-physics-kick-probe")) {
    vec3 lane,dir,wall;check(FindImpactLane(p,lane,dir,wall),"kick outcome lane");
    auto* npc=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},lane,{}));check(npc,"kick outcome NPC");
    npc->DisableEvent("Tick");npc->DisableEvent("Timer");
    std::unique_ptr<RuneNetworkAuthority> net;
    if(!campaign){net=std::make_unique<RuneNetworkAuthority>(game);check(net->bind(1,p,1),"kick outcome authority bind");}
    ScopedRandomSeed seed(82971);
    for(bool playerVictim:{false,true}) {
      unsigned down=0,standing=0,replicas=0;bool landed[2]={false,false};
      for(unsigned trial=0;trial<256;++trial) {
        for(auto* actor:{static_cast<UPawn*>(p),npc}) {
          Release(actor);resistantUntil.erase(actor);actor->Health()=1000;
          auto* health=static_cast<int*>(actor->GetProperty("BodyPartHealth"));for(int j=0;j<15;++j)health[j]=1000;
          actor->Velocity()=actor->Acceleration()=vec3(0);actor->SetBase(nullptr,true);actor->SetPhysics(PHYS_Walking);
          actor->GotoState(actor==p?"PlayerWalking":"Waiting","Begin");
          actor->RuneKickActive=actor->RuneKickPending=false;actor->RuneKickImmunity=0;actor->RuneKickThink=actor->RuneBashThink=10000;
          actor->Dual.guard=false;actor->Dual.guardBlend=0;
          if(actor->HasProperty("bBloodLust"))actor->SetBool("bBloodLust",false);
          if(actor->HasProperty("Strength"))actor->SetInt("Strength",0);
          if(actor->StateFrame)actor->StateFrame->SetLatentState(LatentRunState::Stop);
        }
        auto* target=playerVictim?static_cast<UPawn*>(p):npc;auto* attacker=playerVictim?npc:static_cast<UPawn*>(p);
        check(target->SetLocation(lane)&&attacker->SetLocation(lane-dir*75.f),"kick outcome placement");
        attacker->SetRotation(Rotator::FromVector(dir));target->SetRotation(Rotator::FromVector(-dir));npc->Enemy()=p;
        if(net)net->beginTick(trial+1+(playerVictim?256:0),1000+uint64_t(game.TotalTime*1000));
        RuneKick::CommitImpact(attacker,target,false);bool knocked=Controls(target);npc->Enemy()=nullptr;
        if(net){check(!net->combatFaulted(),"valid host kick result");if(!net->pendingCombat().empty())check(net->releaseCombatThrough(net->pendingCombat().back().id),"release delivered kick result");}
        check(target->Health()<1000&&target->Health()>0,"ordinary kick damage survives");
        if(knocked)++down;else {++standing;check(length(target->Velocity())>100,"standing outcome retains knockback");}
        if(!landed[knocked]) {
          if(net) {
            openrune::net::World captured;check(net->capture(captured),"kick outcome capture");auto ref=net->identity(target);
            openrune::net::Bytes bytes;check(openrune::net::encodeSnapshot({trial+1,0,trial+1,captured},nullptr,bytes),"kick outcome snapshot encode");
            openrune::net::Snapshot decoded;check(openrune::net::decodeSnapshot(bytes,nullptr,decoded),"kick outcome snapshot decode");
            auto state=std::find_if(decoded.world.begin(),decoded.world.end(),[&](const auto& e){return e.ref==ref;});
            check(state!=decoded.world.end()&&state->physical.kind==(knocked?1:0),"host outcome preserved in snapshot");++replicas;
          }
          bool touchedPhysical=knocked;
          for(int frame=0;frame<720;++frame) {
            tick(1);touchedPhysical|=Controls(target);
            for(auto* actor:{static_cast<UPawn*>(p),npc})if(!Controls(actor)&&actor->StateFrame)actor->StateFrame->SetLatentState(LatentRunState::Stop);
          }
          check(!Controls(target)&&target->Health()>0,"kick outcome ends controllable/alive");
          check(knocked||!touchedPhysical,"upright recoil never enters ragdoll");
          check(target->Physics()==PHYS_Walking,"kick outcome lands standing after recovery");landed[knocked]=true;
        }
      }
      check(mode()==Original?down==0:(down>=95&&down<=161&&landed[0]&&landed[1]),"kick outcome distribution is approximately even");
      std::cout<<"RUNE_KICK_OUTCOMES player="<<playerVictim<<" mode="<<mode()<<" down="<<down<<" standing="<<standing<<" snapshots="<<replicas<<"\n";
    }
    std::cout<<"RUNE_PHYSICS_KICK_OK checks="<<checks<<"\n";return;
  }
  if (commandline->HasArg("", "--rune-physics-death-probe")) {
    const bool original = mode() == Original;
    vec3 at = p->Location();
    bool found = false;
    for (auto *spot : game.Level->Actors)
      if (spot && spot->IsA("PlayerStart") &&
          length(spot->Location() - p->Location()) > 180 &&
          p->CheckLocation(spot->Location(), 28, 50, true).first) {
        at = spot->Location();
        found = true;
        break;
      }
    check(found, "death continuity fixture location");
    auto birthCount = [&]() {
      unsigned n = 0;
      for (auto *a : game.Level->Actors)
        if (a && !a->bDeleteMe() && !a->NetworkReplica && a->IsA("RuneCarcass"))
          ++n;
      return n;
    };
    float maxInitial = 0, maxRest = 0;
    unsigned cases = 0;
    for (const char *type :
         {"RuneI.DarkViking", "RuneI.GoblinWarrior", "RuneI.DwarfWoodland"}) {
      auto actors = game.Level->Actors;
      auto *victim = UObject::TryCast<UPawn>(
          p->Spawn(game.packages->FindClass(type), {}, {}, at, {}));
      check(victim, "spawn death continuity victim");
      victim->GotoState("", "");
      victim->DisableEvent("Tick");
      victim->Health() = 5;
      CallEvent(victim, "JointDamaged",
                {ExpressionValue::IntValue(10), ExpressionValue::ObjectValue(p),
                 ExpressionValue::VectorValue(victim->Location()),
                 ExpressionValue::VectorValue(vec3(0)),
                 ExpressionValue::NameValue("blunt"),
                 ExpressionValue::IntValue(0)});
      UActor *body = nullptr;
      for (int f = 0; f < 900 && !body; ++f) {
        tick(1);
        for (auto *a : game.Level->Actors)
          if (a && !a->bDeleteMe() && !a->NetworkReplica &&
              a->IsA("RuneCarcass") &&
              std::find(actors.begin(), actors.end(), a) == actors.end()) {
            body = a;
            break;
          }
      }
      check(body, "ordinary death produces exactly one replacement");
      if (original) {
        check(!Controls(body) && pendingDeaths.empty(),
              "Original death path retains its original ownership");
      } else {
        check(deathObservations.contains(body),
              "natural death handoff captured before drawing");
        auto observation = deathObservations.at(body);
        maxInitial = std::max(maxInitial, observation.initialError);
        check(Controls(body), "replacement is physical immediately at birth");
        auto reference =
            observation.seed.model->JointMatrices(observation.seed.pose);
        auto error = [&]() {
          auto pose = observation.seed.model->ActorJointMatrices(body);
          auto worldNow = GetRuneMeshToWorld(body);
          float worst = 0;
          for (size_t j = 0; j < pose.size(); ++j)
            worst = std::max(
                worst, length(point(observation.seed.world * reference[j]) -
                              point(worldNow * pose[j])));
          return worst;
        };
        float initial = error();
        unsigned asleep = 0;
        for (auto &part : states.at(body).parts)
          asleep += !world->get(part.body).active;
        tick(180);
        float rest = error();
        maxRest = std::max(maxRest, rest);
        std::cout << "DEATH_CONTINUITY class=" << type << " initial=" << initial
                  << " after_rest=" << rest
                  << " seeded_sleep=" << observation.slept
                  << " sleeping_parts=" << asleep << "\n";
        check(observation.slept && initial < .25f && rest < .25f,
              "corpse retains the final world pose without a second fall");
        // Use the real actor lifecycle and snapshot wire path: the receiving
        // client reconstructs translations/scales from the model reference.
        bool wasAuthority=game.NetworkAuthority;
        game.NetworkAuthority=true; // isolated wire check also runs in SP rules
        RuneNetworkAuthority net(game);
        RuneNetworkReplica replica;
        check(net.bind(1, p, 1), "bind death handoff player");
        openrune::net::World captured;
        check(net.capture(captured), "capture natural physical corpse");
        auto bodyRef=net.identity(body);
        std::erase_if(captured,[&](const auto& e){return e.ref!=bodyRef;});
        uint64_t last = 0;
        for (auto &event : net.pendingActors()) {
          last=event.id;
          if(event.ref!=bodyRef)continue;
          if (event.operation == openrune::net::ActorOperation::Spawn)
            check(replica.spawn(game, event), "spawn natural corpse replica");
          else if (event.operation == openrune::net::ActorOperation::Appearance)
            check(replica.appearance(game, event), "natural corpse appearance");
          last = event.id;
        }
        if (last)
          check(net.releaseActorsThrough(last), "release death lifecycle");
        openrune::net::Snapshot snapshot{1, 0, 1, captured}, decoded;
        openrune::net::Bytes wire;
        check(openrune::net::encodeSnapshot(snapshot, nullptr, wire) &&
                  openrune::net::decodeSnapshot(wire, nullptr, decoded),
              "natural death wire round trip");
        check(replica.install(decoded), "install natural death snapshot");
        replica.present(1);
        auto *copy = replica.actor(net.identity(body));
        check(copy && Controls(copy), "natural death replicated physically");
        auto localJoints = model(body)->ActorJointMatrices(body);
        auto remoteJoints = model(copy)->ActorJointMatrices(copy);
        auto localWorld = GetRuneMeshToWorld(body);
        auto remoteWorld = GetRuneMeshToWorld(copy);
        float replicaError = 0;
        auto refPose=RuneCorpse::ReferencePose(*model(body));
        float translationError=0,scaleError=0;
        for(size_t j=1;j<states.at(body).pose.size();++j) {
          translationError=std::max(translationError,length(states.at(body).pose[j].Position-refPose[j].Position));
          scaleError=std::max(scaleError,length(states.at(body).pose[j].Scale-refPose[j].Scale));
        }
        std::cout<<"DEATH_REPLICA_BASIS location_error="<<length(body->Location()-copy->Location())<<" pivot="<<body->PrePivot().z<<","<<copy->PrePivot().z<<" world_error="<<length(point(localWorld)-point(remoteWorld))<<" translation="<<translationError<<" scale="<<scaleError<<"\n";
        for (size_t j = 0; j < localJoints.size(); ++j)
          replicaError = std::max(replicaError,
              length(point(localWorld * localJoints[j]) -
                     point(remoteWorld * remoteJoints[j])));
        std::cout << "DEATH_REPLICA class=" << type
                  << " joint_error=" << replicaError << "\n";
        check(replicaError < .5f, "natural corpse replica preserves final pose");
        for (auto &entity : captured)
          replica.unbind(entity.ref);
        net.unbind(1);
        game.NetworkAuthority=wasAuthority;
        check(RuneCorpse::Throwable(body),
              "settled handoff remains eligible for pickup");
        check(Ragdoll(body, vec3(0)) == false,
              "handoff cannot create a second physical owner");
      }
      body->Destroy();
      ++cases;
    }
    if (!original) {
      auto *source = UObject::TryCast<UPawn>(
          p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {},
                   at + vec3(0, 0, 120), {}));
      check(source, "moving handoff source");
      source->GotoState("", "");
      source->DisableEvent("Tick");
      source->Health() = 0;
      source->SetPhysics(PHYS_Falling);
      source->Velocity() = {190, 40, -110};
      auto *body = UObject::TryCast<UActor>(
          CallEvent(source, "SpawnCarcass").ToObject());
      check(body, "moving original SpawnCarcass");
      adoptDeaths(game.Level);
      check(Controls(body) && !deathObservations.at(body).slept,
            "airborne handoff remains dynamic");
      for (auto &part : states.at(body).parts)
        check(length(world->get(part.body).velocity - source->Velocity()) <
                  .01f,
              "handoff preserves translation without artificial spin impulse");
      body->Destroy();
      source->Destroy();
    }
    unsigned before = birthCount();
    auto *gib = UObject::TryCast<UPawn>(
        p->Spawn(game.packages->FindClass("RuneI.DarkViking"), {}, {}, at, {}));
    check(gib, "gib continuity source");
    gib->Health() = 5;
    gib->GotoState("", "");
    gib->DisableEvent("Tick");
    CallEvent(gib, "JointDamaged",
              {ExpressionValue::IntValue(2000), ExpressionValue::ObjectValue(p),
               ExpressionValue::VectorValue(gib->Location()),
               ExpressionValue::VectorValue(vec3(0)),
               ExpressionValue::NameValue("blunt"),
               ExpressionValue::IntValue(0)});
    tick(720);
    check(birthCount() == before,
          "full gib cannot acquire an intact handoff body");
    std::cout << "RUNE_PHYSICS_DEATH_OK cases=" << cases
              << " initial_error=" << maxInitial << " rest_error=" << maxRest
              << " original=" << original << " checks=" << checks << "\n";
    return;
  }
  if (commandline->HasArg("", "--rune-physics-environment-probe")) {
    vec3 lane, dir, wall;
    bool found = FindImpactLane(p, lane, dir, wall);
    check(found, "authored wall impact lane");
    check(p->SetLocation(lane - dir * 110.f), "place wall-impact instigator");
    p->GotoState("PlayerWalking", "Begin");
    p->SetPhysics(PHYS_Walking);
    p->Velocity() = vec3(0);
    std::cout << "ENV_LANE at=" << lane.x << "," << lane.y << "," << lane.z
              << " direction=" << dir.x << "," << dir.y << " wall=" << wall.x
              << "," << wall.y << "," << wall.z << "\n";
    auto spawnVictim = [&](vec3 at) {
      auto *v = UObject::TryCast<UPawn>(p->Spawn(
          game.packages->FindClass("RuneI.DarkViking"), {}, {}, at, {}));
      check(v, "spawn impact victim");
      v->Health() = 1000;
      v->Enemy() = p;
      v->GotoState("", "");
      v->DisableEvent("Tick");
      return v;
    };
    auto *v = spawnVictim(lane);
    unsigned priorSlams = wallSlams;
    auto knockedAt = game.TotalTime;
    check(Knockdown(v, p, dir * 650.f + vec3(0, 0, 80)),
          "start wall-bound knockdown");
    tick(100);
    std::cout << "ENV_WALL count=" << wallSlams - priorSlams
              << " health=" << v->Health() << "\n";
    check(wallSlams == priorSlams + 1 && v->Health() < 1000 &&
              v->Health() >= 986,
          "one bounded authored-wall hit");
    int afterWall = v->Health();
    tick(40);
    check(v->Health() == afterWall && wallSlams == priorSlams + 1,
          "limb contacts cannot repeat wall damage");
    for (int i = 0; i < 720 && Controls(v); ++i)
      tick(1);
    check(!Controls(v) && v->Health() > 0,
          "wall slam releases player/mob control");
    check(game.TotalTime - knockedAt >= 1.8 && game.TotalTime - knockedAt < 6.2,
          "wall stun is visible and bounded");
    v->Destroy();
    auto *ground = spawnVictim(lane - dir * 90.f);
    priorSlams = wallSlams;
    check(Knockdown(ground, p, vec3(0, 0, 280)), "ground landing setup");
    tick(160);
    check(ground->Health() == 1000 && wallSlams == priorSlams,
          "floor landing is not a wall slam");
    ground->Destroy();
    auto propAt = lane + vec3(0, 0, -22);
    auto *stool =
        p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {}, propAt, {});
    check(stool, "spawn original stool");
    check(stool->GetBool("bDestroyable"),
          "original stool is authored breakable");
    check(Prop(stool, dir * 650.f, p), "launch breakable stool");
    RuneNetworkAuthority net(game);
    RuneNetworkReplica copy;
    openrune::net::EntityRef stoolRef;
    uint64_t snap = 0;
    unsigned effectEvents = 0;
    bool sawDespawn = false;
    auto sync = [&]() {
      openrune::net::World w;
      check(net.capture(w), "capture impact world");
      uint64_t through = 0;
      if (!stoolRef.id)
        stoolRef = net.identity(stool);
      for (auto &e : net.pendingActors()) {
        if (e.operation == openrune::net::ActorOperation::Spawn) {
          check(copy.spawn(game, e), "spawn impact replica");
          if (e.kind == openrune::net::EntityKind::Effect)
            ++effectEvents;
        } else if (e.operation == openrune::net::ActorOperation::Appearance)
          check(copy.appearance(game, e), "impact appearance");
        else {
          if (e.ref == stoolRef)
            sawDespawn = true;
          copy.unbind(e.ref);
        }
        through = e.id;
      }
      if (through)
        check(net.releaseActorsThrough(through), "release impact events");
      openrune::net::Snapshot snapshot{++snap, 0, snap, w}, decoded;
      openrune::net::Bytes wire;
      check(openrune::net::encodeSnapshot(snapshot, nullptr, wire) &&
                openrune::net::decodeSnapshot(wire, nullptr, decoded) &&
                snapshot == decoded,
            "impact wire roundtrip");
      check(copy.install(decoded), "install impact snapshot");
      copy.present(1);
    };
    if (!campaign) {
      check(net.bind(1, p, 1), "bind impact test player");
      sync();
      check(copy.actor(stoolRef), "intact prop replica exists");
    }
    unsigned beforeBroken = brokenProps;
    unsigned debrisPeak = 0;
    for (int i = 0; i < 120; ++i) {
      tick(1);
      unsigned debris = 0;
      for (auto *a : game.Level->Actors)
        if (a && !a->bDeleteMe() && !a->NetworkReplica && a->IsA("DebrisWood"))
          ++debris;
      debrisPeak = std::max(debrisPeak, debris);
      if (!campaign && i % 10 == 0)
        sync();
    }
    std::cout << "ENV_PROP_FINAL replica=" << stool->NetworkReplica
              << " controls=" << Controls(stool)
              << " nodelete=" << int(stool->bNoDelete())
              << " destroyable=" << stool->GetBool("bDestroyable") << " armed="
              << (states.contains(stool) ? states.at(stool).impactArmed : false)
              << " pos=" << stool->Location().x << "," << stool->Location().y
              << "," << stool->Location().z
              << " velocity=" << length(stool->Velocity()) << "\n";
    std::cout << "ENV_BREAK deleted=" << int(stool->bDeleteMe())
              << " breaks=" << brokenProps - beforeBroken
              << " debris=" << debrisPeak
              << " replicated_effects=" << effectEvents
              << " despawn=" << sawDespawn << "\n";
    check(stool->bDeleteMe() && !Controls(stool) &&
              brokenProps == beforeBroken + 1 && debrisPeak > 0,
          "stool shatters through original debris callback");
    if (!campaign)
      check(sawDespawn && !copy.actor(stoolRef) && effectEvents > 0,
            "break/debris effects reach replica without intact duplicate");
    auto *keg =
        p->Spawn(game.packages->FindClass("RuneI.Keg"), {}, {}, propAt, {});
    check(keg, "spawn protected keg");
    keg->SetBool("bDestroyable", false);
    check(Prop(keg, dir * 750.f, p), "indestructible prop can move");
    beforeBroken = brokenProps;
    tick(120);
    check(!keg->bDeleteMe() && Controls(keg) && brokenProps == beforeBroken,
          "authored indestructible survives hard impact");
    bool approachedDrop = false;
    for (int i = 0; i < 32 && !approachedDrop; ++i) {
      float angle = i * 3.14159265f / 16;
      vec3 direction(std::cos(angle), std::sin(angle), 0);
      auto at = keg->Location() - direction * 65.f;
      vec3 floor, n;
      if (!p->Trace(floor, n, at - vec3(0, 0, 120), at + vec3(0, 0, 80), false,
                    vec3(0)) ||
          n.z < .7f)
        continue;
      at = floor + vec3(0, 0, p->CollisionHeight() + 1);
      if (p->CheckLocation(at, p->CollisionRadius(), p->CollisionHeight(), true)
              .first &&
          p->SetLocation(at)) {
        p->SetRotation(Rotator::FromVector(direction));
        approachedDrop = true;
      }
    }
    check(approachedDrop, "approach previously thrown prop");
    p->GotoState("PlayerWalking", "Begin");
    p->SetPhysics(PHYS_Walking);
    p->Velocity() = vec3(0);
    if (auto *proxy = p->GetUObject("AnimProxy"))
      proxy->GotoState("Idle", "Begin");
    for (auto *a : game.Level->Actors)
      if (a && !a->NetworkReplica && !a->bDeleteMe() && a->IsA("Inventory") &&
          !a->Owner() && length(a->Location() - p->Location()) < 120)
        a->bHidden() = true;
    CallEvent(p, "Use");
    check(states.at(keg).carrier == p && !states.at(keg).impactArmed &&
              !states.at(keg).instigator,
          "pickup clears previous impact ownership");
    tick(30);
    CallEvent(p, "Use");
    check(!Carrying(p) && !states.at(keg).impactArmed &&
              !states.at(keg).instigator,
          "Use drop is not an attack");
    keg->SetBool("bDestroyable", true);
    beforeBroken = brokenProps;
    tick(160);
    check(!keg->bDeleteMe() && brokenProps == beforeBroken,
          "gently dropped destroyable prop survives");
    keg->Destroy();
    auto *quest =
        p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {}, propAt, {});
    check(quest, "spawn scripted prop fixture");
    quest->Tag() = "PhysicsProtectedQuest";
    check(!Prop(quest, dir * 650.f, p) && !Controls(quest),
          "tagged scripted scenery never converted");
    quest->Destroy();
    std::cout << "RUNE_PHYSICS_ENVIRONMENT_OK checks=" << checks
              << " wall_slams=" << wallSlams << " broken_props=" << brokenProps
              << "\n";
    return;
  }
  if (commandline->HasArg("", "--rune-physics-interaction-probe")) {
    auto initial = game.Level->Actors;
    vec3 lane = p->Location();
    bool placed = false;
    for (auto *spot : initial)
      if (spot && spot->IsA("PlayerStart")) {
        auto at = spot->Location();
        if (length(at - p->Location()) > 150 &&
            p->CheckLocation(at, 24, 46, true).first) {
          lane = at;
          placed = true;
          break;
        }
      }
    check(placed, "natural death lane");
    auto *victim = UObject::TryCast<UPawn>(p->Spawn(
        game.packages->FindClass("RuneI.DarkViking"), {}, {}, lane, {}));
    check(victim, "spawn natural victim");
    victim->GotoState("", "");
    victim->DisableEvent("Tick");
    victim->Health() = 10;
    CallEvent(victim, "JointDamaged",
              {ExpressionValue::IntValue(20), ExpressionValue::ObjectValue(p),
               ExpressionValue::VectorValue(victim->Location()),
               ExpressionValue::VectorValue(vec3(0)),
               ExpressionValue::NameValue("blunt"),
               ExpressionValue::IntValue(0)});
    tick(840);
    UActor *body = nullptr;
    for (auto *a : game.Level->Actors)
      if (a && !a->bDeleteMe() && a->IsA("RuneCarcass") &&
          std::find(initial.begin(), initial.end(), a) == initial.end()) {
        body = a;
        break;
      }
    check(body, "natural death produced carcass");
    std::cout << "INTERACTION_BODY throwable=" << RuneCorpse::Throwable(body)
              << " controls=" << Controls(body)
              << " state=" << body->GetStateName().ToString()
              << " physics=" << int(body->Physics())
              << " anim=" << body->AnimSequence().ToString()
              << " finished=" << int(body->bAnimFinished())
              << " frame=" << body->AnimFrame() << " rate=" << body->AnimRate()
              << " size=" << body->CollisionRadius() << ","
              << body->CollisionHeight() << " hidden=" << int(body->bHidden())
              << " active=" << body->CorpseBody.active << "\n";
    bool approached = false;
    for (int i = 0; i < 32 && !approached; ++i) {
      float angle = i * 3.14159265f / 16;
      vec3 d(std::cos(angle), std::sin(angle), 0);
      auto at = body->Location() - d * 65.f;
      vec3 floor, normal;
      if (!p->Trace(floor, normal, at - vec3(0, 0, 160), at + vec3(0, 0, 90),
                    false, vec3(0)) ||
          normal.z < .7f)
        continue;
      at = floor + vec3(0, 0, p->CollisionHeight() + 1);
      if (p->CheckLocation(at, p->CollisionRadius(), p->CollisionHeight(), true)
              .first &&
          p->SetLocation(at)) {
        p->SetRotation(Rotator::FromVector(d));
        approached = true;
      }
    }
    check(approached, "approach natural corpse");
    p->GotoState("PlayerWalking", "Begin");
    p->SetPhysics(PHYS_Walking);
    p->Velocity() = vec3(0);
    if (auto *proxy = p->GetUObject("AnimProxy"))
      proxy->GotoState("Idle", "Begin");
    for (int i = 0; i < 5 && !RuneCorpse::Held(p); ++i) {
      CallEvent(p, "Use");
      tick(120);
    }
    std::cout << "INTERACTION_PICKUP held=" << (RuneCorpse::Held(p) == body)
              << " eligible=" << RuneCorpse::Eligible(p)
              << " throwable=" << RuneCorpse::Throwable(body)
              << " distance=" << length(body->Location() - p->Location())
              << "\n";
    check(RuneCorpse::Held(p) == body,
          "Use lifts naturally killed physical body");
    CallEvent(p, "Throw");
    tick(65);
    check(!RuneCorpse::Held(p) && Controls(body),
          "natural body throw returns to physics");
    CallEvent(body, "JointDamaged",
              {ExpressionValue::IntValue(2000), ExpressionValue::ObjectValue(p),
               ExpressionValue::VectorValue(body->Location()),
               ExpressionValue::VectorValue(vec3(0)),
               ExpressionValue::NameValue("blunt"),
               ExpressionValue::IntValue(0)});
    tick(120);
    check(body->bDeleteMe() && !Controls(body),
          "gibbed physical corpse is removed");
    // A real kick notify can punt a loose prop; it cannot punt one behind
    // the player or change a hidden/deleted body back into visible geometry.
    auto *stool = p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {},
                           p->Location() + vec3(65, 0, -22), {});
    check(stool && Prop(stool, vec3(0)), "spawn kickable stool");
    p->GotoState("PlayerWalking", "Begin");
    p->SetPhysics(PHYS_Walking);
    p->SetRotation(Rotator(0, 32768, 0));
    check(!KickObject(p), "kick rejects objects behind the player");
    p->SetRotation(Rotator(0, 0, 0));
    check(RuneKick::Start(p), "prop kick starts normal animation");
    check(RuneKick::Notify(p), "actual kick notify dispatches prop impact");
    check(length(world->get(states.at(stool).parts[0].body).velocity) > 500,
          "prop kick imparts visible momentum");
    check(states.at(stool).instigator == p,
          "prop impact retains combat instigator");
    tick(45);
    for (bool knocked : {false, true}) {
      auto before = game.Level->Actors;
      auto *v = UObject::TryCast<UPawn>(p->Spawn(
          game.packages->FindClass("RuneI.DarkViking"), {}, {}, lane, {}));
      check(v, "spawn gib victim");
      v->GotoState("Fighting", "Begin");
      v->Enemy() = p;
      v->DisableEvent("Tick");
      v->Health() = 100;
      if (knocked) {
        check(Knockdown(v, p, {100, 0, 150}), "pre-gib knockdown");
        tick(10);
      }
      CallEvent(
          v, "JointDamaged",
          {ExpressionValue::IntValue(2000), ExpressionValue::ObjectValue(p),
           ExpressionValue::VectorValue(v->Location()),
           ExpressionValue::VectorValue(vec3(0)),
           ExpressionValue::NameValue("blunt"), ExpressionValue::IntValue(0)});
      tick(840);
      int corpses = 0;
      for (auto *a : game.Level->Actors)
        if (a && !a->bDeleteMe() && a->IsA("RuneCarcass") &&
            std::find(before.begin(), before.end(), a) == before.end())
          ++corpses;
      std::cout << "INTERACTION_GIB knocked=" << knocked
                << " deleted=" << int(v->bDeleteMe())
                << " hidden=" << int(v->bHidden()) << " corpses=" << corpses
                << " controls=" << Controls(v) << "\n";
      check(corpses == 0 && !Controls(v),
            "full gib cannot leave an intact corpse");
    }
    std::cout << "RUNE_PHYSICS_INTERACTION_OK checks=" << checks << "\n";
    return;
  }
  auto *m = model(p);
  check(humanoid(m), "Ragnar profile");
  RuneNetworkAuthority net(game);
  check(net.bind(1, p, 1), "bind physics player");
  RuneNetworkReplica replica;
  uint64_t snapshotId = 0;
  unsigned replicatedPoses = 0;
  auto snapshot = [&]() {
    CurrentScope sampleAuthoritativePose;
    openrune::net::World w;
    check(net.capture(w), "capture physical world");
    uint64_t last = 0;
    for (auto &e : net.pendingActors()) {
      if (e.operation == openrune::net::ActorOperation::Spawn)
        check(replica.spawn(game, e), "spawn physical replica");
      else if (e.operation == openrune::net::ActorOperation::Appearance)
        check(replica.appearance(game, e), "physical appearance");
      else
        replica.unbind(e.ref);
      last = e.id;
    }
    if (last)
      check(net.releaseActorsThrough(last), "release physical actors");
    openrune::net::Snapshot s{++snapshotId, 0, snapshotId, w}, decoded;
    openrune::net::Bytes wire;
    check(openrune::net::encodeSnapshot(s, nullptr, wire) &&
              openrune::net::decodeSnapshot(wire, nullptr, decoded) &&
              s == decoded,
          "physical wire round trip");
    check(replica.install(decoded), "install physical snapshot");
    replica.present(1);
    for (auto &e : w)
      if (e.physical.kind) {
        auto *original = game.Level->Actors[0];
        for (auto *a : game.Level->Actors)
          if (!a->NetworkReplica && net.identity(a) == e.ref) {
            original = a;
            break;
          }
        auto *copy = replica.actor(e.ref);
        check(copy && Controls(copy), "physical replica pose active");
        auto *m = model(original);
        auto *n = model(copy);
        auto a = m->ActorJointMatrices(original),
             b = n->ActorJointMatrices(copy);
        auto x = GetRuneMeshToWorld(original), y = GetRuneMeshToWorld(copy);
        for (size_t j = 0; j < a.size(); ++j)
          check(length(point(x * a[j]) - point(y * b[j])) < .5f,
                "replicated physical joints match world pose");
        ++replicatedPoses;
      }
  };

  for (size_t i = 0; i < m->Joints.size(); ++i)
    std::cout << "PHYSICS_JOINT index=" << i
              << " parent=" << m->Joints[i].Parent
              << " name=" << m->Joints[i].Name.ToString() << "\n";
  auto origin = p->Location();
  auto rest = RuneCorpse::ReferencePose(*m);
  {auto laws=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=laws&~RuneBattleLaws::Knockdown;
   check(!Knockdown(p,nullptr,{200,50,180})&&!Controls(p),"disabled law rejects otherwise eligible living ragdoll");RuneBattleLaws::Campaign=laws;}
  check(Knockdown(p, nullptr, {200, 50, 180}), "living knockdown starts");
  check(Controls(p), "physics owns knocked pawn");
  unsigned interpolatedFrames = 0;
  vec3 lastPhysics = p->Location(),
       lastRendered =
           point(GetRuneMeshToWorld(p) * m->ActorJointMatrices(p)[1]);
  for (int i = 0; i < 720; ++i) {
    tick(1);
    if (i % 20 == 0)
      snapshot();
    auto pose = m->ActorPose(p);
    vec3 rendered;
    {
      PresentationScope present;
      rendered = point(GetRuneMeshToWorld(p) * m->ActorJointMatrices(p)[1]);
    }
    if (Controls(p) && p->Location() == lastPhysics &&
        length(rendered - lastRendered) > .001f)
      ++interpolatedFrames;
    lastPhysics = p->Location();
    lastRendered = rendered;
    check(pose.size() == rest.size(), "pose topology");
    for (size_t j = 1; Controls(p) && j < pose.size(); ++j) {
      check(length(pose[j].Position - rest[j].Position) < .0001f,
            "ragdoll retains bone translation");
      check(length(pose[j].Scale - rest[j].Scale) < .0001f,
            "ragdoll retains bone scale");
    }
    check(std::isfinite(p->Location().z), "finite ragdoll root");
  }
  check(!Controls(p) && p->Health() > 0, "living pawn recovers");
  check(interpolatedFrames > 5,
        "physical rendering advances between fixed ticks");
  check(length(p->Location() - origin) < 900,
        "recovery stays near physical landing");
  UActor *carcass = nullptr;
  for (auto *spot : game.Level->Actors)
    if (spot && spot->IsA("PlayerStart")) {
      carcass = p->Spawn(UObject::Cast<UClass>(p->GetUObject("CarcassType")),
                         {}, {}, spot->Location(), {});
      if (carcass)
        break;
    }
  check(carcass, "spawn original carcass");
  CallEvent(carcass, "InitFor", {ExpressionValue::ObjectValue(p)});
  tick(40);
  check(Controls(carcass), "ordinary carcass enters physical mode");
  float maxSpeed = 0;
  for (int i = 0; i < 600; ++i) {
    tick(1);
    if (i % 30 == 0)
      snapshot();
    check(std::isfinite(carcass->Location().z), "finite corpse");
    maxSpeed = std::max(maxSpeed, length(carcass->Velocity()));
  }
  check(maxSpeed < 5000, "constrained corpse does not explode");
  auto *stool = p->Spawn(game.packages->FindClass("RuneI.Stool"), {}, {},
                         carcass->Location() + vec3(0, 0, 70), {});
  check(stool, "spawn original prop");
  check(Prop(stool, {100, 0, 200}, p), "prop becomes rigid body");
  tick(360);
  snapshot();
  check(Controls(stool) && std::isfinite(stool->Location().z),
        "prop settles physically");
  carcass->bHidden() = true; // Keep this pickup fixture focused on the prop.
  Release(stool);
  stool->SetPhysics(PHYS_None);
  bool propLane = false;
  for (int i = 0; i < 16 && !propLane; ++i) {
    float angle = i * 3.14159265f / 8;
    vec3 direction(std::cos(angle), std::sin(angle), 0);
    auto at = p->Location() + direction * 65.f;
    if (!p->FastTrace(at, p->Location()) ||
        !p->CheckLocation(at, 24, 30, true).first)
      continue;
    if (stool->SetLocation(at)) {
      p->SetRotation(Rotator::FromVector(direction));
      propLane = true;
    }
  }
  check(propLane, "place prop in a clear reachable lane");
  check(Prop(stool, vec3(0), p), "reactivate prop");
  for (auto *a : game.Level->Actors)
    if (a && !a->bDeleteMe() && a->IsA("Inventory") && !a->Owner() &&
        length(a->Location() - p->Location()) < 140)
      a->bHidden() = true;
  p->GotoState("PlayerWalking", "Begin");
  p->SetPhysics(PHYS_Walking);
  if (auto *proxy = p->GetUObject("AnimProxy"))
    proxy->GotoState("Idle", "Begin");
  CallEvent(p, "Use");
  check(Carrying(p), "Use event lifts prop");
  tick(30);
  snapshot();
  check(RuneCorpse::Busy(p) && RuneCorpse::SpeedScale(p) < .7f,
        "held prop uses carry pose and reduced movement");
  CallEvent(p, "Throw");
  check(Carrying(p), "Throw event retains visible wind-up");
  tick(12);
  check(Carrying(p), "prop remains held through early wind-up");
  tick(30);
  snapshot();
  check(!Carrying(p) && stool->Owner() == nullptr,
        "prop releases without changing weapon ownership");
  tick(240);
  auto before = Stats();
  BeforeSave();
  check(states.empty(), "save boundary restores original actor ownership");
  RequestedMode = Original;
  tick(1);
  check(!world && mode() == 0, "original mode removes extra world");
  std::cout << "RUNE_PHYSICS_PROBE_OK checks=" << checks
            << " interpolated_frames=" << interpolatedFrames
            << " replicated_poses=" << replicatedPoses
            << " corpse_speed=" << maxSpeed << " bodies=" << before.bodies
            << " step_ms=" << before.stepMs << "\n";
#endif
}
} // namespace RunePhysics

#ifdef OPENRUNE_ENABLE_NETWORK
namespace RunePhysics {
bool Capture(UActor *a, openrune::net::PhysicalPose &out) {
  CurrentScope sampleAuthoritativePose;
  auto it = states.find(a);
  if (it == states.end() || it->second.replica)
    return false;
  auto &s = it->second;
  out = {};
  out.kind = s.prop ? 2 : s.basis.empty() ? 1 : 3;
  out.basis = s.basis;
  if (s.prop) {
    auto m = MeshToWorld(a);
    auto offset = point(m) - a->Location();
    for (int k = 0; k < 3; ++k)
      out.root[k] =
          int16_t(std::clamp(std::lround(offset[k] * 8), -32767l, 32767l));
    auto q = rotationOf(m);
    float v[] = {q.x, q.y, q.z, q.w};
    for (int k = 0; k < 4; ++k)
      out.rotation[k] = int16_t(std::lround(v[k] * 32767));
  } else {
    for (int k = 0; k < 3; ++k)
      out.root[k] = int16_t(
          std::clamp(std::lround(s.pose[0].Position[k] * 8), -32767l, 32767l));
    for (auto &p : s.pose)
      out.joints.push_back({uint16_t(p.Rotation.Pitch),
                            uint16_t(p.Rotation.Yaw),
                            uint16_t(p.Rotation.Roll)});
  }
  return true;
}
bool ValidateReplica(UActor *a, const openrune::net::PhysicalPose &p) {
  if (!p.kind)
    return true;
  auto *m = model(a);
  return a && a->NetworkReplica && m &&
         (p.kind == 2 || ((p.kind == 1 || p.kind == 3) && p.joints.size() == m->Joints.size()));
}
void Present(UActor *a, const openrune::net::PhysicalPose &before,
             const openrune::net::PhysicalPose &after, float alpha) {
  if (!a || !a->NetworkReplica)
    return;
  if (!after.kind) {
    erase(a, false);
    return;
  }
  if (!ValidateReplica(a, after))
    return;
  clearPuppet(a);
  auto &s = states[a];
  s.actor = a;
  s.model = model(a);
  s.replica = true;
  s.prop = after.kind == 2;
  s.carrier = s.prop ? UObject::TryCast<UPawn>(a->Owner()) : nullptr;
  alpha = std::clamp(alpha, 0.f, 1.f);
  bool blend =
      before.kind == after.kind && before.joints.size() == after.joints.size();
  if (!blend)
    alpha = 1;
  vec3 root(0);
  for (int k = 0; k < 3; ++k)
    root[k] = ((blend ? before.root[k] : after.root[k]) * (1 - alpha) +
               after.root[k] * alpha) /
              8.f;
  if (s.prop) {
    auto r = [](auto &p) {
      return quaternion(p.rotation[0] / 32767.f, p.rotation[1] / 32767.f,
                        p.rotation[2] / 32767.f, p.rotation[3] / 32767.f);
    };
    auto q = normalize(blend ? slerp(r(before), r(after), alpha) : r(after));
    s.replicaWorld = mat4::translate(a->Location() + root) *
                     mat4::quaternion(q.x, q.y, q.z, q.w) *
                     mat4::scale(a->DrawScale());
  } else {
    s.pose = RuneCorpse::ReferencePose(*s.model);
    for(const auto& b:after.basis) {
      if(b.joint>=s.pose.size())return;
      for(int k=0;k<3;++k) {
        s.pose[b.joint].Position[k]=b.position[k]/64.f;
        s.pose[b.joint].Scale[k]=b.scale[k]/4096.f;
      }
    }
    for (size_t j = 0; j < s.pose.size(); ++j) {
      auto to = s.pose[j], from = to;
      auto b = after.joints[j], c = blend ? before.joints[j] : b;
      from.Rotation = {c[0], c[1], c[2]};
      to.Rotation = {b[0], b[1], b[2]};
      s.pose[j] = BlendRunePose(from, to, alpha);
    }
    s.pose[0].Position = root;
  }
}
} // namespace RunePhysics
#endif
