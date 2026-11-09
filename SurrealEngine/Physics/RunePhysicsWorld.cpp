#include "RunePhysicsWorld.h"
// Jolt requires its umbrella header before every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>

namespace RuneRigid {
namespace {
JPH::Vec3 v(vec3 p) { return {p.x, p.y, p.z}; }
JPH::RVec3 pos(vec3 p) { return JPH::RVec3(v(p) / UnitsPerMeter); }
vec3 game(JPH::Vec3Arg p) {
  return {p.GetX() * UnitsPerMeter, p.GetY() * UnitsPerMeter,
          p.GetZ() * UnitsPerMeter};
}
JPH::Quat q(Rotation r) { return JPH::Quat(r.x, r.y, r.z, r.w).Normalized(); }
Rotation rotation(JPH::QuatArg r) {
  return {r.GetX(), r.GetY(), r.GetZ(), r.GetW()};
}
bool finite(vec3 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
bool finite(Rotation r) {
  double n=double(r.x)*r.x+double(r.y)*r.y+double(r.z)*r.z+double(r.w)*r.w;
  return std::isfinite(n)&&n>1e-12&&n<1e30;
}
class Broad final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
    return JPH::BroadPhaseLayer(l == 0 ? 0 : 1);
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
    return l.GetValue() == 0 ? "Static" : "Moving";
  }
#endif
};
class ObjectBroad final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer l,
                     JPH::BroadPhaseLayer b) const override {
    return l != 0 || b.GetValue() != 0;
  }
};
class Pairs final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return (a == 1 || b == 1) && (a != 0 || b != 0);
  }
};
void initialize() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory;
    JPH::RegisterTypes();
  });
}
} // namespace
struct World::Impl final : public JPH::ContactListener {
  Broad broad;
  ObjectBroad objectBroad;
  Pairs pairs;
  JPH::TempAllocatorImpl allocator{8 * 1024 * 1024};
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem system;
  JPH::Ref<JPH::GroupFilterTable> groups = new JPH::GroupFilterTable(1);
  std::vector<Handle> handles;
  std::vector<JPH::Ref<JPH::Constraint>> constraints;
  std::vector<Contact> events;
  double seconds = 0;
  std::map<std::pair<uint64_t, uint64_t>, double> ignored;
  JPH::ValidateResult
  OnContactValidate(const JPH::Body &a, const JPH::Body &b, JPH::RVec3Arg,
                    const JPH::CollideShapeResult &) override {
    auto av = a.GetUserData(), bv = b.GetUserData();
    auto key = std::minmax(av, bv);
    return ignored.contains({key.first, key.second})
               ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
               : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
  }
  Impl() {
    system.Init(4096, 0, 16384, 8192, broad, objectBroad, pairs);
    system.SetGravity({0, 0, -23});
    system.SetContactListener(this);
  }
  void record(const JPH::Body &a, const JPH::Body &b,
              const JPH::ContactManifold &m) {
    if (events.size() >= 256 || m.mRelativeContactPointsOn1.empty())
      return;
    auto point = m.GetWorldSpaceContactPointOn1(0);
    auto relative = a.GetPointVelocity(point) - b.GetPointVelocity(point);
    float speed = std::abs(relative.Dot(m.mWorldSpaceNormal)) * UnitsPerMeter;
    if (speed < 20)
      return;
    events.push_back({a.GetUserData(), b.GetUserData(), game(JPH::Vec3(point)),
                      game(m.mWorldSpaceNormal) / UnitsPerMeter, speed,
                      game(a.GetPointVelocity(point)), game(b.GetPointVelocity(point))});
  }
  void OnContactAdded(const JPH::Body &a, const JPH::Body &b,
                      const JPH::ContactManifold &m,
                      JPH::ContactSettings &) override {
    record(a, b, m);
  }
  void OnContactPersisted(const JPH::Body &a, const JPH::Body &b,
                          const JPH::ContactManifold &m,
                          JPH::ContactSettings &) override {
    record(a, b, m);
  }
};
World::World() {
  initialize();
  impl = std::make_unique<Impl>();
}
World::~World() = default;
void World::reset() { impl = std::make_unique<Impl>(); }
bool World::geometry(const std::vector<Triangle> &triangles) {
  JPH::TriangleList list;
  list.reserve(triangles.size());
  for (auto &t : triangles)
    if (finite(t.a) && finite(t.b) && finite(t.c) &&
        length(cross(t.b - t.a, t.c - t.a)) > .001f)
      list.emplace_back(v(t.a) / UnitsPerMeter, v(t.b) / UnitsPerMeter,
                        v(t.c) / UnitsPerMeter);
  if (list.empty())
    return false;
  auto shape = JPH::MeshShapeSettings(list).Create();
  if (shape.HasError())
    throw std::runtime_error(shape.GetError().c_str());
  JPH::BodyCreationSettings settings(shape.Get(), JPH::RVec3::sZero(),
                                     JPH::Quat::sIdentity(),
                                     JPH::EMotionType::Static, 0);
  auto id = impl->system.GetBodyInterface().CreateAndAddBody(
      settings, JPH::EActivation::DontActivate);
  if (id.IsInvalid())
    return false;
  impl->handles.push_back(id.GetIndexAndSequenceNumber());
  impl->system.OptimizeBroadPhase();
  return true;
}
Handle World::mesh(const std::vector<Triangle> &triangles, vec3 at,
                   Rotation rotation, uint64_t owner) {
  JPH::TriangleList list;
  for (auto &t : triangles)
    if (finite(t.a) && finite(t.b) && finite(t.c) &&
        length(cross(t.b - t.a, t.c - t.a)) > .001f)
      list.emplace_back(v(t.a) / UnitsPerMeter, v(t.b) / UnitsPerMeter,
                        v(t.c) / UnitsPerMeter);
  if (list.empty())
    return Invalid;
  auto shape = JPH::MeshShapeSettings(list).Create();
  if (shape.HasError())
    return Invalid;
  JPH::BodyCreationSettings settings(shape.Get(), pos(at), q(rotation),
                                     JPH::EMotionType::Kinematic, 2);
  settings.mUserData = owner;
  auto id = impl->system.GetBodyInterface().CreateAndAddBody(
      settings, JPH::EActivation::DontActivate);
  if (id.IsInvalid())
    return Invalid;
  auto h = id.GetIndexAndSequenceNumber();
  impl->handles.push_back(h);
  return h;
}
void World::motion(Handle h, Motion m) {
  if (contains(h)) {
    auto &b = impl->system.GetBodyInterface();
    b.SetMotionType(JPH::BodyID(h),
                    m == Motion::Kinematic ? JPH::EMotionType::Kinematic
                                           : JPH::EMotionType::Dynamic,
                    JPH::EActivation::Activate);
    b.SetObjectLayer(JPH::BodyID(h), m == Motion::Kinematic ? 2 : 1);
  }
}
Handle World::create(const Description &d) {
  if (!finite(d.position) || !finite(d.velocity) || !finite(d.rotation) || !std::isfinite(d.mass) ||
      d.mass <= 0)
    throw std::runtime_error("Invalid physics body");
  JPH::RefConst<JPH::Shape> shape;
  if (d.sphere)
    shape = new JPH::SphereShape(std::max(.02f, d.radius / UnitsPerMeter));
  else if (d.cylinder)
    shape =
        new JPH::CylinderShape(std::max(.02f, d.halfHeight / UnitsPerMeter),
                               std::max(.02f, d.radius / UnitsPerMeter), .01f);
  else if (d.capsule)
    shape = new JPH::CapsuleShape(std::max(.02f, d.halfHeight / UnitsPerMeter),
                                  std::max(.02f, d.radius / UnitsPerMeter));
  else
    shape = new JPH::BoxShape(JPH::Vec3::sMax(v(d.halfExtent) / UnitsPerMeter,
                                              JPH::Vec3::sReplicate(.02f)),
                              .01f);
  auto motion = d.motion == Motion::Static      ? JPH::EMotionType::Static
                : d.motion == Motion::Kinematic ? JPH::EMotionType::Kinematic
                                                : JPH::EMotionType::Dynamic;
  JPH::BodyCreationSettings settings(
      shape, pos(d.position), q(d.rotation), motion,
      motion == JPH::EMotionType::Static      ? 0
      : motion == JPH::EMotionType::Kinematic ? 2
                                              : 1);
  settings.mUserData = d.owner;
  settings.mFriction = d.friction;
  settings.mRestitution = d.restitution;
  settings.mOverrideMassProperties =
      JPH::EOverrideMassProperties::CalculateInertia;
  settings.mMassPropertiesOverride.mMass = d.mass;
  settings.mLinearVelocity = v(d.velocity) / UnitsPerMeter;
  settings.mLinearDamping = .08f;
  settings.mAngularDamping = .25f;
  settings.mMaxLinearVelocity = 80;
  settings.mMaxAngularVelocity = 35;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
  if (d.group)
    settings.mCollisionGroup = JPH::CollisionGroup(impl->groups, d.group, 0);
  auto id = impl->system.GetBodyInterface().CreateAndAddBody(
      settings, motion == JPH::EMotionType::Static
                    ? JPH::EActivation::DontActivate
                    : JPH::EActivation::Activate);
  if (id.IsInvalid())
    return Invalid;
  auto h = id.GetIndexAndSequenceNumber();
  impl->handles.push_back(h);
  return h;
}
bool World::contains(Handle h) const {
  return h != Invalid && std::find(impl->handles.begin(), impl->handles.end(),
                                   h) != impl->handles.end();
}
void World::remove(Handle h) {
  if (!contains(h))
    return;
  auto &bi = impl->system.GetBodyInterface();
  for (auto it = impl->constraints.begin(); it != impl->constraints.end();) {
    auto *c = static_cast<JPH::TwoBodyConstraint *>(it->GetPtr());
    if (c->GetBody1()->GetID() == JPH::BodyID(h) ||
        c->GetBody2()->GetID() == JPH::BodyID(h)) {
      impl->system.RemoveConstraint(c);
      it = impl->constraints.erase(it);
    } else
      ++it;
  }
  bi.RemoveBody(JPH::BodyID(h));
  bi.DestroyBody(JPH::BodyID(h));
  std::erase(impl->handles, h);
}
Transform World::get(Handle h) const {
  Transform t;
  if (!contains(h))
    return t;
  auto &b = impl->system.GetBodyInterface();
  auto id = JPH::BodyID(h);
  t.position = game(JPH::Vec3(b.GetPosition(id)));
  t.rotation = rotation(b.GetRotation(id));
  t.velocity = game(b.GetLinearVelocity(id));
  auto a = b.GetAngularVelocity(id);
  t.angular = {a.GetX(), a.GetY(), a.GetZ()};
  t.active = b.IsActive(id);
  return t;
}
bool World::recover(Handle h,const Transform& safe) {
  if(!contains(h)||!finite(safe.position)||!finite(safe.rotation))return false;
  auto id=JPH::BodyID(h);auto& bi=impl->system.GetBodyInterface();
  bi.SetPositionAndRotation(id,pos(safe.position),q(safe.rotation),JPH::EActivation::Activate);
  bi.SetLinearAndAngularVelocity(id,JPH::Vec3::sZero(),JPH::Vec3::sZero());
  bi.InvalidateContactCache(id);
  for(auto& constraint:impl->constraints){auto* c=static_cast<JPH::TwoBodyConstraint*>(constraint.GetPtr());if(c->GetBody1()->GetID()==id||c->GetBody2()->GetID()==id)c->ResetWarmStart();}
  return true;
}
void World::place(Handle h, vec3 p, Rotation r, bool activate) {
  if (contains(h) && finite(p) && finite(r))
    impl->system.GetBodyInterface().SetPositionAndRotation(
        JPH::BodyID(h), pos(p), q(r),
        activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}
void World::velocity(Handle h, vec3 p) {
  if (contains(h) && finite(p))
    impl->system.GetBodyInterface().SetLinearVelocity(JPH::BodyID(h),
                                                      v(p) / UnitsPerMeter);
}
void World::angularVelocity(Handle h, vec3 p) {
  if (contains(h) && finite(p))
    impl->system.GetBodyInterface().SetAngularVelocity(JPH::BodyID(h), v(p));
}
void World::impulse(Handle h, vec3 f, vec3 p) {
  if (contains(h) && finite(f) && finite(p))
    impl->system.GetBodyInterface().AddImpulse(JPH::BodyID(h),
                                               v(f) / UnitsPerMeter, pos(p));
}
void World::kinematic(Handle h, vec3 p, Rotation r, float dt) {
  if (contains(h) && finite(p) && finite(r) && std::isfinite(dt) && dt > 0)
    impl->system.GetBodyInterface().MoveKinematic(JPH::BodyID(h), pos(p), q(r),
                                                  dt);
}
void World::wake(Handle h) {
  if (contains(h))
    impl->system.GetBodyInterface().ActivateBody(JPH::BodyID(h));
}
void World::sleep(Handle h) {
  if (contains(h))
    impl->system.GetBodyInterface().DeactivateBody(JPH::BodyID(h));
}
void World::constrain(Handle parent, Handle child, vec3 anchor, vec3 axis,
                      float swing, float twist) {
  if (!contains(parent) || !contains(child))
    return;
  // The bridge is owned and stepped by one game thread. Taking two hashed
  // body mutexes independently could deadlock when their buckets coincide.
  JPH::BodyLockWrite a(impl->system.GetBodyLockInterfaceNoLock(),
                       JPH::BodyID(parent));
  JPH::BodyLockWrite b(impl->system.GetBodyLockInterfaceNoLock(),
                       JPH::BodyID(child));
  if (!a.Succeeded() || !b.Succeeded())
    throw std::runtime_error("Physics constraint body lock");
  auto direction = v(axis).NormalizedOr(JPH::Vec3::sAxisY());
  auto plane = direction.GetNormalizedPerpendicular();
  JPH::SwingTwistConstraintSettings s;
  s.mPosition1 = s.mPosition2 = pos(anchor);
  s.mTwistAxis1 = s.mTwistAxis2 = direction;
  s.mPlaneAxis1 = s.mPlaneAxis2 = plane;
  s.mNormalHalfConeAngle = s.mPlaneHalfConeAngle = swing;
  s.mTwistMinAngle = -twist;
  s.mTwistMaxAngle = twist;
  s.mMaxFrictionTorque = .4f;
  auto *c = s.Create(a.GetBody(), b.GetBody());
  impl->constraints.emplace_back(c);
  impl->system.AddConstraint(c);
}
void World::ignore(uint64_t a, uint64_t b, float seconds) {
  if (a && b && a != b && seconds > 0) {
    auto k = std::minmax(a, b);
    impl->ignored[{k.first, k.second}] = impl->seconds + std::min(seconds, 1.f);
  }
}
void World::gravity(vec3 g) {
  if (finite(g))
    impl->system.SetGravity(v(g) / UnitsPerMeter);
}
void World::step(float dt) {
  impl->events.clear();
  if (dt <= 0 || dt > .05f)
    return;
  impl->seconds += dt;
  std::erase_if(impl->ignored,
                [&](auto &p) { return p.second <= impl->seconds; });
  auto error = impl->system.Update(dt, 1, &impl->allocator, &impl->jobs);
  if (error != JPH::EPhysicsUpdateError::None)
    throw std::runtime_error("Physics contact capacity exceeded");
}
const std::vector<Contact> &World::contacts() const { return impl->events; }
unsigned World::activeBodies() const {
  return impl->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}
unsigned World::bodies() const { return impl->system.GetNumBodies(); }
Rotation between(vec3 a, vec3 b) {
  return rotation(JPH::Quat::sFromTo(v(a).NormalizedOr(JPH::Vec3::sAxisY()),
                                     v(b).NormalizedOr(JPH::Vec3::sAxisY())));
}
Rotation multiply(Rotation a, Rotation b) { return rotation(q(a) * q(b)); }
Rotation inverse(Rotation a) { return rotation(q(a).Conjugated()); }
vec3 rotate(Rotation r, vec3 p) {
  auto t = q(r) * v(p);
  return {t.GetX(), t.GetY(), t.GetZ()};
}
} // namespace RuneRigid
