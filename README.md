# Torque

Native LeviLauncher mod: Sable-style rigid-body physics for Minecraft Bedrock,
using Jolt Physics (vendored in `third_party/JoltPhysics`, MIT license).

## Stage 1 (this commit)
- Jolt runs a single test cube (gravity, floor, tumbling) in `src/Physics.cpp`.
- `src/torque.cpp` hooks `FallingBlockRenderer`'s render function and applies
  the cube's rotation to the falling block's matrix, logging the matrix too.
- Switches (`kApplyRotation`, `kPivot`, `kTranslationInLastRow`) are at the top
  of `torque.cpp` / `MatrixMath.h`.

## Build
Push to GitHub; the Actions workflow builds and uploads `torque-*.levipack`.

## Per-game-update work
Addresses live in `src/Builds.h`, one entry per Minecraft build. `enable()` tries
each entry's slot-3 signature and uses the first that matches, so after an
update you add an entry and keep the old ones. Re-find the addresses from slot 3
of `FallingBlockRenderer`'s vtable: getStack, push, accessor, pop are the calls
around the matrix math (details in the header comment).
