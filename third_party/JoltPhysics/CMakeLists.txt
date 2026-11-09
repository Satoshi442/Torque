# Thin wrapper around Jolt's library definition (Jolt/Jolt.cmake).
# Jolt's own Build/CMakeLists.txt also builds samples/tests and overrides global
# compiler flags (-Werror, -fno-rtti, LTO), none of which we want in a mod.
# Jolt.cmake sets CMAKE_CXX_STANDARD 17; this directory scope keeps that local.
set(PHYSICS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
include("${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake")
