#include "UseOnProbe.h"

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include <unistd.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace pl::memory;

namespace {

// ---- Switches --------------------------------------------------------------
// 0 = pure passthrough: hook installed, nothing logged, nothing read.
// 1 = log raw register values only (no memory reads).
// 2 = also peek at what x1 / x7 point to (uses the pipe trick below).
// If the game crashes on a stick click, set this to 0 and rebuild: if it still
// crashes, the hook itself is the problem; if not, raise it one step at a time.
constexpr int kMode = 1;

// Lines go to this file with open/write/close on every call, so they are on
// disk even if the game dies right after.
constexpr const char *kLogPath =
    "/storage/emulated/0/Android/media/org.levimc.launcher/torque_probe.txt";

// Base Item vtable slot 132 = Item::_useOn (VA 0x1079c8a0 on this build).
// Slot 86 is Item::dispense (dispensers only), so it is not hooked.
constexpr const char *SIG_SLOT132 =
    "FF C3 01 D1 FD 7B 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 F4 4F 06 A9 "
    "FD 83 00 91 5A D0 3B D5 F8 03 00 AA F3 03 07 AA 48 ?? ?? F9 F4 03 06 2A "
    "F5 03 05 2A F6 03 02 AA F7 03 01 AA A8 83 1F F8 00 ?? ?? F9 E3 ?? ?? F9";

// _useOn(ItemStack&, Actor&, BlockPos pos, uchar face, HandSlot, Vec3 const&)
// returns InteractionResult in x0. Registers: x0 this, x1 ItemStack*, x2 Actor*,
// x3:x4 BlockPos by value, x5 face, x6 HandSlot, x7 Vec3*. Nothing is on the
// stack, so forwarding x0..x7 forwards everything.
using Fn = uint64_t (*)(void *, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t);

Fn gOrig132 = nullptr;
HookHandle gHook132;
std::atomic<int> gLogged{0};
constexpr int kMaxLogs = 40;

void fileLog(const char *fmt, ...) {
  FILE *f = std::fopen(kLogPath, "a");
  if (!f)
    return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f); // closes and flushes
}

decltype(auto) log() { return ll::mod::NativeMod::current()->getLogger(); }

// Read memory without crashing on a bad pointer, using only open/write/read:
// write() from the address into a pipe fails with EFAULT instead of faulting.
// (process_vm_readv is avoided: Android's app seccomp filter may kill the
// process on syscalls it doesn't allow.)
bool safeRead(uint64_t addr, void *out, size_t n) {
  if (addr < 0x10000 || n > 256)
    return false;
  static int fds[2] = {-1, -1};
  if (fds[0] < 0 && pipe(fds) != 0) {
    fds[0] = fds[1] = -1;
    return false;
  }
  if (write(fds[1], reinterpret_cast<const void *>(addr), n) !=
      static_cast<ssize_t>(n))
    return false;
  return read(fds[0], out, n) == static_cast<ssize_t>(n);
}

void dump(void *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
          uint64_t a5, uint64_t a6, uint64_t a7) {
  if (gLogged.fetch_add(1) >= kMaxLogs)
    return;

  fileLog("[slot132] this=%#llx x1=%#llx x2=%#llx x3=%#llx x4=%#llx x5=%#llx "
          "x6=%#llx x7=%#llx",
          (unsigned long long)(uintptr_t)self, (unsigned long long)a1,
          (unsigned long long)a2, (unsigned long long)a3,
          (unsigned long long)a4, (unsigned long long)a5,
          (unsigned long long)a6, (unsigned long long)a7);
  // x3:x4 = BlockPos by value, x5 low byte = face, x6 = HandSlot.
  fileLog("[slot132] pos=(%d, %d, %d) face=%u hand=%u", int32_t(a3),
          int32_t(a3 >> 32), int32_t(a4), unsigned(a5 & 0xff), unsigned(a6));

  if (kMode < 2)
    return;

  uint64_t vtbl = 0;
  if (safeRead(reinterpret_cast<uint64_t>(self), &vtbl, 8))
    fileLog("[slot132] vtbl=%#llx", (unsigned long long)vtbl);

  float f[3]{};
  if (safeRead(a7, f, sizeof(f)))
    fileLog("[slot132] *x7 as Vec3 = %.3f %.3f %.3f", f[0], f[1], f[2]);

  uint64_t q[6]{};
  if (safeRead(a1, q, sizeof(q)))
    fileLog("[slot132] *x1 = %#llx %#llx %#llx %#llx %#llx %#llx",
            (unsigned long long)q[0], (unsigned long long)q[1],
            (unsigned long long)q[2], (unsigned long long)q[3],
            (unsigned long long)q[4], (unsigned long long)q[5]);
}

uint64_t det132(void *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                uint64_t a5, uint64_t a6, uint64_t a7) {
  if (kMode >= 1)
    dump(self, a1, a2, a3, a4, a5, a6, a7);
  return gOrig132(self, a1, a2, a3, a4, a5, a6, a7);
}

} // namespace

namespace torque {

bool installUseOnProbe() {
  const uintptr_t a132 = resolveSignature(SIG_SLOT132, "libminecraftpe.so");
  log().info("useOn probe (mode {}): slot132 at {:#x}", kMode, a132);
  fileLog("useOn probe installed, mode %d, slot132 at %#llx", kMode,
          (unsigned long long)a132);
  if (!a132) {
    log().error("useOn probe: slot132 signature not found");
    return false;
  }
  gHook132 = HookHandle(reinterpret_cast<FuncPtr>(a132),
                        reinterpret_cast<FuncPtr>(&det132),
                        reinterpret_cast<FuncPtr *>(&gOrig132));
  if (!gHook132.installed()) {
    log().error("useOn probe: hook failed");
    return false;
  }
  return true;
}

void removeUseOnProbe() { gHook132.reset(); }

} // namespace torque
