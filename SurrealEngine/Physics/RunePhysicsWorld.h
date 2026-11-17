#pragma once
#include "Math/vec.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace RuneRigid {
using Handle = uint32_t;
constexpr Handle Invalid = 0xffffffffu;
constexpr float UnitsPerMeter = 50.f;
struct Rotation {
  float x = 0, y = 0, z = 0, w = 1;
};
struct Triangle {
  vec3 a, b, c;
};
struct Transform {
  vec3 position{0}, velocity{0}, angular{0};
  Rotation rotation;
  bool active = false;
};
struct Contact {
  uint64_t a = 0, b = 0;
  vec3 point{0}, normal{0};
  float speed = 0;
  vec3 velocityA{0}, velocityB{0}; // Pre-solver point velocities, in Rune units.
};
enum class Motion { Static, Kinematic, Dynamic };
struct Description {
  vec3 position{0}, halfExtent{5}, velocity{0};
  Rotation rotation;
  float radius = 4, halfHeight = 8, mass = 10, friction = .65f,
        restitution = .12f;
  uint64_t owner = 0;
  uint32_t group = 0;
  Motion motion = Motion::Dynamic;
  bool capsule = false, sphere = false, cylinder = false;
  // Cosmetic puppets collide with scenery, never push gameplay bodies.
  bool cosmetic = false;
};
class World {
public:
  World();
  ~World();
  World(const World &) = delete;
  World &operator=(const World &) = delete;
  void reset();
  bool geometry(const std::vector<Triangle> &);
  Handle create(const Description &);
  Handle mesh(const std::vector<Triangle> &, vec3, Rotation, uint64_t owner);
  void motion(Handle, Motion);
  void remove(Handle);
  bool contains(Handle) const;
  Transform get(Handle) const;
  bool recover(Handle,const Transform& lastValid);
  void place(Handle, vec3, Rotation, bool activate = true);
  void velocity(Handle, vec3);
  void angularVelocity(Handle, vec3);
  void impulse(Handle, vec3 impulse, vec3 point);
  void kinematic(Handle, vec3, Rotation, float dt);
  void wake(Handle);
  void sleep(Handle);
  void constrain(Handle parent, Handle child, vec3 anchor, vec3 twistAxis,
                 float swing, float twist);
  // Motor target is the child body's orientation relative to its parent (not
  // the joint's constraint frame). Units are Hz, damping ratio, and N m.
  bool drive(Handle child, Rotation relative, float frequency, float damping,
             float maxTorque, bool relaxLimits = false);
  bool releaseDrive(Handle child);
  void ignore(uint64_t ownerA, uint64_t ownerB, float seconds);
  void gravity(vec3);
  void step(float dt);
  const std::vector<Contact> &contacts() const;
  unsigned activeBodies() const;
  unsigned bodies() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
Rotation between(vec3 from, vec3 to);
Rotation multiply(Rotation, Rotation);
Rotation inverse(Rotation);
vec3 rotate(Rotation, vec3);
} // namespace RuneRigid
