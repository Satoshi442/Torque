#pragma once
// Thin wrapper around Jolt. No Jolt headers leak out of Physics.cpp.
namespace torque {

struct Vec3f { float x, y, z; };
struct Quatf { float x, y, z, w; };
struct Pose { Vec3f pos; Quatf rot; };

class Physics {
public:
  static Physics &get();

  void init();
  void shutdown();

  // Call with real elapsed seconds. Runs fixed 60 Hz steps internally.
  // Single-threaded: call from one thread only (the render thread here).
  void update(float realDt);

  // Pose of the test cube (stage 1: one cube drives every falling block).
  Pose cubePose() const;

private:
  struct Impl;
  Impl *mImpl = nullptr;
  void resetCube();
};

} // namespace torque
