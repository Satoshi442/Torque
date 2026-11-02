#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <memory>

#include "Physics.h"

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace {

// ---- Tunables (stage 1) ----------------------------------------------------
constexpr float kStep = 1.0f / 60.0f;  // fixed physics step
constexpr float kGravity = -30.0f;     // blocks/s^2 (Minecraft-ish, not 9.81)
constexpr float kDropHeight = 6.0f;    // cube start height above the floor
constexpr float kCycleSeconds = 6.0f;  // re-drop the cube so the test loops

// ---- Layers: two is enough (static world, moving bodies) -------------------
namespace Layers {
constexpr ObjectLayer NON_MOVING = 0;
constexpr ObjectLayer MOVING = 1;
constexpr ObjectLayer NUM_LAYERS = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr BroadPhaseLayer NON_MOVING(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint NUM_LAYERS(2);
} // namespace BroadPhaseLayers

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
  bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
    switch (a) {
    case Layers::NON_MOVING:
      return b == Layers::MOVING;
    case Layers::MOVING:
      return true;
    default:
      return false;
    }
  }
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
  BPLayerInterfaceImpl() {
    mMap[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mMap[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }
  uint GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
    return mMap[layer];
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
    return (BroadPhaseLayer::Type)layer == 0 ? "NON_MOVING" : "MOVING";
  }
#endif
private:
  BroadPhaseLayer mMap[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final
    : public ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override {
    switch (a) {
    case Layers::NON_MOVING:
      return b == BroadPhaseLayers::MOVING;
    case Layers::MOVING:
      return true;
    default:
      return false;
    }
  }
};

} // namespace

namespace torque {

struct Physics::Impl {
  std::unique_ptr<TempAllocatorImpl> temp;
  std::unique_ptr<JobSystemSingleThreaded> jobs;
  BPLayerInterfaceImpl bpLayers;
  ObjectVsBroadPhaseLayerFilterImpl objVsBp;
  ObjectLayerPairFilterImpl objPair;
  PhysicsSystem system;
  BodyID floorId;
  BodyID cubeId;
  float acc = 0.0f;
  float age = 0.0f;
};

Physics &Physics::get() {
  static Physics p;
  return p;
}

void Physics::init() {
  if (mImpl)
    return;

  RegisterDefaultAllocator();
  Factory::sInstance = new Factory();
  RegisterTypes();

  mImpl = new Impl();
  mImpl->temp = std::make_unique<TempAllocatorImpl>(4 * 1024 * 1024);
  mImpl->jobs = std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);

  constexpr uint cMaxBodies = 1024;
  constexpr uint cNumBodyMutexes = 0; // 0 = default
  constexpr uint cMaxBodyPairs = 1024;
  constexpr uint cMaxContactConstraints = 1024;
  mImpl->system.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                     cMaxContactConstraints, mImpl->bpLayers, mImpl->objVsBp,
                     mImpl->objPair);
  mImpl->system.SetGravity(Vec3(0.0f, kGravity, 0.0f));

  // Stage 1 floor: a big static slab whose top face is at y = 0.
  BodyInterface &bi = mImpl->system.GetBodyInterface();
  BodyCreationSettings floor(new BoxShape(Vec3(50.0f, 1.0f, 50.0f)),
                             RVec3(0.0f, -1.0f, 0.0f), Quat::sIdentity(),
                             EMotionType::Static, Layers::NON_MOVING);
  mImpl->floorId = bi.CreateAndAddBody(floor, EActivation::DontActivate);

  resetCube();
  mImpl->system.OptimizeBroadPhase();
}

void Physics::shutdown() {
  if (!mImpl)
    return;

  BodyInterface &bi = mImpl->system.GetBodyInterface();
  for (BodyID id : {mImpl->cubeId, mImpl->floorId}) {
    if (!id.IsInvalid()) {
      bi.RemoveBody(id);
      bi.DestroyBody(id);
    }
  }
  delete mImpl;
  mImpl = nullptr;

  UnregisterTypes();
  delete Factory::sInstance;
  Factory::sInstance = nullptr;
}

void Physics::resetCube() {
  BodyInterface &bi = mImpl->system.GetBodyInterface();
  if (!mImpl->cubeId.IsInvalid()) {
    bi.RemoveBody(mImpl->cubeId);
    bi.DestroyBody(mImpl->cubeId);
  }

  // Slightly tilted with some spin, so it visibly tumbles instead of landing flat.
  BodyCreationSettings s(
      new BoxShape(Vec3(0.5f, 0.5f, 0.5f)), RVec3(0.0f, kDropHeight, 0.0f),
      Quat::sRotation(Vec3(1.0f, 0.0f, 1.0f).Normalized(), 0.7f),
      EMotionType::Dynamic, Layers::MOVING);
  s.mRestitution = 0.35f;
  s.mFriction = 0.6f;
  s.mAngularVelocity = Vec3(2.0f, 1.0f, 3.0f);
  mImpl->cubeId = bi.CreateAndAddBody(s, EActivation::Activate);
  mImpl->age = 0.0f;
}

void Physics::update(float realDt) {
  if (!mImpl)
    return;

  realDt = std::clamp(realDt, 0.0f, 0.1f); // avoid a spiral after a hitch
  mImpl->acc += realDt;
  mImpl->age += realDt;

  while (mImpl->acc >= kStep) {
    mImpl->system.Update(kStep, 1, mImpl->temp.get(), mImpl->jobs.get());
    mImpl->acc -= kStep;
  }

  if (mImpl->age > kCycleSeconds)
    resetCube();
}

Pose Physics::cubePose() const {
  Pose out{{0, 0, 0}, {0, 0, 0, 1}};
  if (!mImpl || mImpl->cubeId.IsInvalid())
    return out;

  const BodyInterface &bi = mImpl->system.GetBodyInterface();
  const RVec3 p = bi.GetPosition(mImpl->cubeId);
  const Quat q = bi.GetRotation(mImpl->cubeId);
  out.pos = {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
  out.rot = {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
  return out;
}

} // namespace torque
