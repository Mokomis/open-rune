#include "Physics/RunePhysicsWorld.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace RuneRigid;
int main() {
  try {
    auto check = [](bool ok, const char *why) {
      if (!ok)
        throw std::runtime_error(why);
    };
    World w;
    std::vector<Triangle> floor{
        {{-1000, -1000, 0}, {1000, -1000, 0}, {1000, 1000, 0}},
        {{-1000, -1000, 0}, {1000, 1000, 0}, {-1000, 1000, 0}}};
    check(w.geometry(floor), "world mesh");
    Description box;
    box.position = {0, 0, 150};
    box.halfExtent = {12, 10, 8};
    box.velocity = {130, 0, 0};
    box.owner = 10;
    auto b = w.create(box);
    w.angularVelocity(b, {2, 3, 1});
    bool contact = false;
    for (int i = 0; i < 1200; ++i) {
      w.step(1.f / 120);
      auto t = w.get(b);
      check(std::isfinite(t.position.z) && t.position.z > -1,
            "box cannot tunnel through floor");
      contact |= !w.contacts().empty();
    }
    auto t = w.get(b);
    check(contact && t.position.z > 5 && t.position.z < 15,
          "tumbling box lands");
    check(!t.active, "resting box sleeps");
    w.impulse(b, {0, 0, 2500}, t.position);
    w.step(1.f / 120);
    check(w.get(b).velocity.z > 100, "sleeping prop wakes when thrown");
    Description part;
    part.capsule = true;
    part.radius = 4;
    part.halfHeight = 8;
    part.mass = 6;
    part.group = 8;
    part.position = {0, 0, 130};
    auto a = w.create(part);
    part.position = {0, 20, 130};
    auto c = w.create(part);
    w.constrain(a, c, {0, 10, 130}, {0, 1, 0}, 1.2f, .5f);
    w.impulse(a, {3500, 0, 3000}, {0, 0, 130});
    float maxSeparation = 0;
    for (int i = 0; i < 1200; ++i) {
      w.step(1.f / 120);
      auto x = w.get(a), y = w.get(c);
      maxSeparation = std::max(maxSeparation, length(x.position - y.position));
      check(std::isfinite(x.rotation.w) && x.position.z > -10 &&
                y.position.z > -10,
            "constrained tumble remains finite and colliding");
    }
    check(maxSeparation < 45, "joint stays connected under impulse");
    // A recovery restores positions/rotations and clears solver/contact warm
    // starts, while preserving the body IDs and their articulated constraints.
    auto safeA=w.get(a),safeC=w.get(c);
    w.velocity(a,{1000,200,300});w.angularVelocity(a,{10,3,2});
    check(w.recover(a,safeA)&&w.recover(c,safeC),"recover articulated bodies");
    check(length(w.get(a).position-safeA.position)<.001f&&length(w.get(a).velocity)<.001f&&length(w.get(a).angular)<.001f,"recovery uses last known pose and clears motion");
    auto invalid=safeA;invalid.rotation={0,0,0,0};check(!w.recover(a,invalid),"invalid recovery rotation rejected");
    invalid=safeA;invalid.position.x=std::numeric_limits<float>::quiet_NaN();check(!w.recover(a,invalid),"invalid recovery position rejected");
    Description bad;bad.rotation={0,0,0,0};bool rejected=false;
    try{w.create(bad);}catch(const std::runtime_error&){rejected=true;}check(rejected,"zero quaternion never enters solver");
    bad.rotation={0,0,0,std::numeric_limits<float>::quiet_NaN()};rejected=false;
    try{w.create(bad);}catch(const std::runtime_error&){rejected=true;}check(rejected,"nonfinite quaternion never enters solver");
    bad.rotation={1e30f,0,0,1};rejected=false;
    try{w.create(bad);}catch(const std::runtime_error&){rejected=true;}check(rejected,"overflowing quaternion never enters solver");
    w.impulse(a,{3500,0,3000},w.get(a).position);
    for(int step=0;step<240;++step){w.step(1.f/120);auto x=w.get(a),y=w.get(c);check(std::isfinite(x.velocity.x)&&std::isfinite(y.velocity.x)&&length(x.position-y.position)<45,"repaired joint still simulates");}
    w.remove(a);
    check(!w.contains(a) && w.contains(c),
          "removing a limb removes its constraints safely");
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 96; ++i) {
      part.position = {float(i % 12) * 12, float(i / 12) * 15,
                       60 + float(i % 5) * 20};
      part.group = 0;
      w.create(part);
    }
    for (int i = 0; i < 600; ++i)
      w.step(1.f / 120);
    auto ms = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - start)
                  .count() /
              600;
    std::cout << "CINEMATIC_WORLD_OK floor=1 throw=1 sleep=1 joint_separation="
              << maxSeparation << " live_bodies=" << w.bodies()
              << " stress_step_ms=" << ms << "\n";
    w.reset();
    check(w.bodies() == 0, "map reset clears physics");
    check(w.geometry(floor), "new level floor");
    std::vector<Triangle> deck{{{-80, -80, 0}, {80, -80, 0}, {80, 80, 0}},
                               {{-80, -80, 0}, {80, 80, 0}, {-80, 80, 0}}};
    auto platform = w.mesh(deck, {0, 0, 40}, {}, 100);
    check(platform != Invalid, "kinematic platform mesh");
    Description ball;
    ball.sphere = true;
    ball.radius = 7;
    ball.position = {0, 0, 100};
    auto rider = w.create(ball);
    for (int i = 0; i < 240; ++i)
      w.step(1.f / 120);
    check(std::abs(w.get(rider).position.z - 47) < 2,
          "rigid body rests on lift");
    for (int i = 0; i < 240; ++i) {
      w.kinematic(platform, {0, 0, 40 + i * .25f}, {}, 1.f / 120);
      w.step(1.f / 120);
    }
    check(w.get(rider).position.z > 100, "lift transports physical body");
    std::cout << "CINEMATIC_LIFT_OK rider_z=" << w.get(rider).position.z
              << "\n";
    w.reset();
    std::vector<Triangle> wall{
        {{100, -2000, 0}, {100, -2000, 500}, {100, 2000, 500}},
        {{100, -2000, 0}, {100, 2000, 500}, {100, 2000, 0}}};
    check(w.geometry(wall), "impact wall geometry");
    w.gravity(vec3(0));
    ball.position = {0, 0, 80};
    ball.velocity = {100, 600, 0};
    ball.owner = 200;
    auto glance = w.create(ball);
    float glancing = 0;
    bool tangent = false;
    for (int i = 0; i < 180; ++i) {
      w.step(1.f / 120);
      for (const auto &c : w.contacts())
        if (std::abs(c.normal.z) < .4f) {
          glancing = std::max(glancing, c.speed);
          auto velocity = c.a == 200 ? c.velocityA : c.velocityB;
          tangent |= std::abs(velocity.y) > 450;
        }
    }
    check(tangent && glancing > 50 && glancing < 150,
          "glancing speed uses wall normal, not fast tangent");
    w.remove(glance);
    ball.position = {0, 0, 80};
    ball.velocity = {650, 0, 0};
    ball.owner = 201;
    auto slam = w.create(ball);
    float incoming = 0, after = 0;
    for (int i = 0; i < 60; ++i) {
      w.step(1.f / 120);
      for (const auto &c : w.contacts())
        if (c.speed > incoming) {
          incoming = c.speed;
          after = length(w.get(slam).velocity);
        }
    }
    check(incoming > 500 && after < incoming * .5f,
          "pre-solver impact retained after collision stops body");
    std::cout << "CINEMATIC_IMPACT_OK glancing=" << glancing
              << " head_on=" << incoming << " post_collision=" << after << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "CINEMATIC_WORLD_FAILED " << e.what() << "\n";
    return 1;
  }
}
