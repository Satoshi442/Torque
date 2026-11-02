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
#include "MatrixMath.h"
#include "Physics.h"

using namespace pl::memory;

namespace {

// ---- Switches --------------------------------------------------------------
constexpr bool kApplyRotation = true; // false = log only, no visual change
constexpr int kLogFirstFrames = 6;    // log this many render calls...
constexpr int kLogEvery = 300;        // ...then every Nth one

// Pivot in the block's model space. (0.5,0.5,0.5) assumes the mesh spans
// 0..1; use (0,0,0) if the mesh is centered on the origin.
constexpr torque::Vec3f kPivot{0.5f, 0.5f, 0.5f};

// ---- Build-specific addresses ---------------------------------------------
constexpr const char *SIG_SLOT3 =
    "EA 0F 18 FC E9 23 01 6D FD 7B 02 A9 FC 6F 03 A9 FA 67 04 A9 F8 5F 05 A9 "
    "F6 57 06 A9 F4 4F 07 A9 FD 83 00 91 FF 43 0B D1 5C D0 3B D5 88 ?? ?? F9 "
    "A8 03 1D F8 59 ?? ?? F9";

constexpr uintptr_t RVA_SLOT3     = 0xab6a574;  // FallingBlockRenderer render
constexpr uintptr_t RVA_GETSTACK  = 0xae04bb0;  // ctx -> MatrixStack*
constexpr uintptr_t RVA_PUSH      = 0x11a191b8; // push copy of top, returns Ref
constexpr uintptr_t RVA_ACCESSOR  = 0x11a199ac; // Ref -> Matrix*, marks dirty
constexpr uintptr_t RVA_POP       = 0x11a1987c; // ~Ref, pops

// FIX 1: Removed the ~Ref() {} destructor. 
// Making this a "trivial" type ensures the ARM64 compiler returns it in 
// standard registers (x0/x1) rather than forcing a hidden x8 (sret) pointer.
struct Ref {
    void *stack;
    float *m;
};

using GetStackFn = void *(*)(void *ctx);
using PushFn     = Ref (*)(void *stack, bool arg);
using AccessorFn = float *(*)(Ref *);
using PopFn      = void (*)(Ref *);
using Slot3Fn    = void *(*)(void *self, void *ctx, void *renderData);

uintptr_t gBase = 0;
Slot3Fn gSlot3Orig = nullptr;
pl::memory::HookHandle gHook;
int gCalls = 0;

decltype(auto) log() { return ll::mod::NativeMod::current()->getLogger(); }

void *slot3Detour(void *self, void *ctx, void *renderData) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last_render = clock::now();
    const auto now = clock::now();
    const float dt = std::chrono::duration<float>(now - last_render).count();
    last_render = now;

    // FIX 2: Throttle physics updates. 
    // Because this hook runs per-entity, multiple falling blocks would cause 
    // the physics engine to update multiple times per frame. This ensures it 
    // only steps the simulation once every ~16ms (approx 60Hz).
    static clock::time_point last_physics_update = clock::now();
    if (now - last_physics_update > std::chrono::milliseconds(16)) {
        float physics_dt = std::chrono::duration<float>(now - last_physics_update).count();
        torque::Physics::get().update(physics_dt);
        last_physics_update = now;
    }

    auto getStack  = reinterpret_cast<GetStackFn>(gBase + RVA_GETSTACK);
    auto push      = reinterpret_cast<PushFn>(gBase + RVA_PUSH);
    auto accessor  = reinterpret_cast<AccessorFn>(gBase + RVA_ACCESSOR);
    auto pop       = reinterpret_cast<PopFn>(gBase + RVA_POP);

    void *stack = getStack(ctx);
    if (!stack)
        return gSlot3Orig(self, ctx, renderData);

    Ref ref = push(stack, false);
    float *m = accessor(&ref);

    const torque::Pose pose = torque::Physics::get().cubePose();
    const bool doLog = gCalls < kLogFirstFrames || gCalls % kLogEvery == 0;

    if (doLog) {
        // FIX 3: Disabled the renderData memory read. 
        // If the +0x10 offset is wrong for your specific game build, reading 
        // it causes an instant SIGSEGV crash. We log the cube pose instead.
        /*
        const float *pos = reinterpret_cast<const float *>(
            reinterpret_cast<uintptr_t>(renderData) + 0x10);
        */

        log().info("call {} | cube pos {:.2f} {:.2f} {:.2f} q {:.3f} {:.3f} {:.3f} {:.3f}",
                   gCalls, pose.pos.x, pose.pos.y, pose.pos.z, 
                   pose.rot.x, pose.rot.y, pose.rot.z, pose.rot.w);
                   
        for (int r = 0; r < 4; ++r)
            log().info(" m[{}..{}] {:.3f} {:.3f} {:.3f} {:.3f}", r * 4, r * 4 + 3,
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
        const uintptr_t slot3 = resolveSignature(SIG_SLOT3, "libminecraftpe.so");
        if (!slot3) {
            mSelf.getLogger().error("slot3 signature not found (game updated?)");
            return false;
        }

        gBase = slot3 - RVA_SLOT3; // module base from one known RVA

        gHook = HookHandle(reinterpret_cast<FuncPtr>(slot3),
                           reinterpret_cast<FuncPtr>(&slot3Detour),
                           reinterpret_cast<FuncPtr *>(&gSlot3Orig));

        if (!gHook.installed()) {
            mSelf.getLogger().error("failed to install slot3 hook");
            return false;
        }

        mSelf.getLogger().info("slot3 hooked at {:#x}, base {:#x}", slot3, gBase);
        return true;
    }

    bool disable() {
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
