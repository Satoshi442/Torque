// Torque - stage 1.
// Hooks FallingBlockRenderer's render function. Each frame it steps Jolt and
// rotates the falling block's matrix with the pose of a Jolt test cube, so a
// falling block should tumble. It also logs the matrix so the layout and the
// pivot can be checked against the real game.
//
// All addresses below are for ONE game build. They were found by following
// slot 3 of FallingBlockRenderer's vtable (see README.md).
#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include <chrono>
#include <cstdint>

#include "Builds.h"
#include "MatrixMath.h"
#include "Physics.h"
#include "UseOnProbe.h"

using namespace pl::memory;

namespace {

// ---- Switches --------------------------------------------------------------
constexpr bool kApplyRotation = true;   // false = log only, no visual change
constexpr int kLogFirstFrames = 6;      // log this many render calls...
constexpr int kLogEvery = 300;          // ...then every Nth one
// Pivot in the block's model space. (0.5,0.5,0.5) assumes the mesh spans
// 0..1; use (0,0,0) if the mesh is centered on the origin.
constexpr torque::Vec3f kPivot{0.5f, 0.5f, 0.5f};

// ---- Build-specific addresses live in Builds.h (see the header for how to add one)

// MatrixStack::Ref. The user-provided destructor is deliberate: it makes the
// type non-trivial so the compiler returns it via x8 (sret) like the game
// does. We call the game's real pop ourselves; this destructor does nothing.
struct Ref {
  void *stack;
  float *m;
  ~Ref() {}
};

using GetStackFn = void *(*)(void *ctx);
using PushFn = Ref (*)(void *stack, bool arg);
using AccessorFn = float *(*)(Ref *);
using PopFn = void (*)(Ref *);
using Slot3Fn = void *(*)(void *self, void *ctx, void *renderData);

uintptr_t gBase = 0;
const torque::BuildInfo *gBuild = nullptr; // set in enable()
Slot3Fn gSlot3Orig = nullptr;
pl::memory::HookHandle gHook;
int gCalls = 0;

decltype(auto) log() { return ll::mod::NativeMod::current()->getLogger(); }

void *slot3Detour(void *self, void *ctx, void *renderData) {
  using clock = std::chrono::steady_clock;
  static clock::time_point last = clock::now();
  const auto now = clock::now();
  const float dt = std::chrono::duration<float>(now - last).count();
  last = now;
  torque::Physics::get().update(dt);

  auto getStack = reinterpret_cast<GetStackFn>(gBase + gBuild->rvaGetStack);
  auto push = reinterpret_cast<PushFn>(gBase + gBuild->rvaPush);
  auto accessor = reinterpret_cast<AccessorFn>(gBase + gBuild->rvaAccessor);
  auto pop = reinterpret_cast<PopFn>(gBase + gBuild->rvaPop);

  void *stack = getStack(ctx);
  if (!stack)
    return gSlot3Orig(self, ctx, renderData);

  // Push our own copy of the top matrix, edit it, let the original render
  // (it pushes its own copy of OUR top), then pop ours.
  Ref ref = push(stack, false);
  float *m = accessor(&ref);
  const torque::Pose pose = torque::Physics::get().cubePose();

  const bool doLog = gCalls < kLogFirstFrames || gCalls % kLogEvery == 0;
  if (doLog) {
    const float *pos = reinterpret_cast<const float *>(
        reinterpret_cast<uintptr_t>(renderData) + 0x10); // ActorRenderData Vec3
    log().info("call {} renderPos {:.3f} {:.3f} {:.3f} | cube pos {:.2f} {:.2f} "
               "{:.2f} q {:.3f} {:.3f} {:.3f} {:.3f}",
               gCalls, pos[0], pos[1], pos[2], pose.pos.x, pose.pos.y,
               pose.pos.z, pose.rot.x, pose.rot.y, pose.rot.z, pose.rot.w);
    for (int r = 0; r < 4; ++r)
      log().info("  m[{}..{}] {:.3f} {:.3f} {:.3f} {:.3f}", r * 4, r * 4 + 3,
                 m[r * 4], m[r * 4 + 1], m[r * 4 + 2], m[r * 4 + 3]);
  }
  ++gCalls;

  if (kApplyRotation)
    torque::applyPivotRotation(m, pose.rot, kPivot);

  void *result = gSlot3Orig(self, ctx, renderData);
  pop(&ref);
  return result;
}

} // namespace

class Torque {
public:
  static Torque &instance() {
    static Torque t;
    return t;
  }
  Torque() : mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    torque::Physics::get().init();
    return true;
  }

  bool enable() {
    uintptr_t slot3 = 0;
    for (int i = 0; i < torque::kBuildCount; ++i) {
      slot3 = resolveSignature(torque::kBuilds[i].sigSlot3, "libminecraftpe.so");
      if (slot3) {
        gBuild = &torque::kBuilds[i];
        mSelf.getLogger().info("matched {}", gBuild->name);
        break;
      }
    }
    if (!slot3) {
      mSelf.getLogger().error(
          "slot3 signature matched none of {} known builds (game updated? "
          "add an entry to Builds.h)",
          torque::kBuildCount);
      return false;
    }
    gBase = slot3 - gBuild->rvaSlot3; // module base from the matched build's RVA
    gHook = HookHandle(reinterpret_cast<FuncPtr>(slot3),
                       reinterpret_cast<FuncPtr>(&slot3Detour),
                       reinterpret_cast<FuncPtr *>(&gSlot3Orig));
    if (!gHook.installed()) {
      mSelf.getLogger().error("failed to install slot3 hook");
      return false;
    }
    mSelf.getLogger().info("slot3 hooked at {:#x}, base {:#x}", slot3, gBase);
    torque::installUseOnProbe(); // log-only; failure must not stop stage 1
    return true;
  }

  bool disable() {
    torque::removeUseOnProbe();
    gHook.reset();
    return true;
  }

  bool unload() {
    torque::Physics::get().shutdown();
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
};

PL_REGISTER_MOD(Torque, Torque::instance())
