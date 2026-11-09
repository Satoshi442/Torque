#pragma once
// Per-game-build data for the render hook.
//
// Each entry = one Minecraft build. enable() tries the entries in order, picks
// the first whose slot-3 signature matches, and uses THAT entry's addresses.
// So supporting a new game build means adding an entry here, nothing else.
//
// To add a build (keep older entries so old builds keep working):
//   1. Run Zaphkiel on the new libminecraftpe.so: vfc<20FallingBlockRenderer>
//   2. Slot 3 of the vtable = rvaSlot3 (fingerprint: big function, reads a Vec3
//      at renderData+0x10, push ... accessor ... pop of the matrix stack).
//   3. In that function's call order: getStack = call right before push,
//      push = returns Ref via x8, accessor = right after push, pop = last call.
//   4. fn<rvaSlot3> in sig_targets.txt gives sigSlot3.
#include <cstdint>

namespace torque {

struct BuildInfo {
  const char *name;
  const char *sigSlot3;      // signature of FallingBlockRenderer render (slot 3)
  uintptr_t rvaSlot3;        // its RVA; module base = matched address - rvaSlot3
  uintptr_t rvaGetStack;     // ctx -> MatrixStack*
  uintptr_t rvaPush;         // push copy of top, returns Ref
  uintptr_t rvaAccessor;     // Ref -> Matrix*, marks stack dirty
  uintptr_t rvaPop;          // ~Ref, pops
};

inline constexpr BuildInfo kBuilds[] = {
    {
        "build A (slot3 @ 0xab6a574)",
        // Unique on this build; embeds the frame size (sub sp, sp, #0x2d0), so a
        // frame change on update makes it fail instead of matching wrongly.
        "EA 0F 18 FC E9 23 01 6D FD 7B 02 A9 FC 6F 03 A9 FA 67 04 A9 F8 5F 05 A9 "
        "F6 57 06 A9 F4 4F 07 A9 FD 83 00 91 FF 43 0B D1 5C D0 3B D5 88 ?? ?? F9 "
        "A8 03 1D F8 59 ?? ?? F9",
        0xab6a574,
        0xae04bb0,
        0x11a191b8,
        0x11a199ac,
        0x11a1987c,
    },
    // Older build (slot3 @ 0xa70745c): getStack 0xa9534d0, push 0x110afbc0,
    // accessor 0x110b03b4, pop 0x110b0284. No signature was generated for it,
    // so it is not listed. Add it here if you need that build again.
};

inline constexpr int kBuildCount = sizeof(kBuilds) / sizeof(kBuilds[0]);

} // namespace torque
