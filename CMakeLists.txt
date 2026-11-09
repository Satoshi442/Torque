cmake_minimum_required(VERSION 3.22)
project(torque LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(MOD_ID "torque" CACHE STRING "Package directory name / runtime mod id")
set(MOD_NAME "Torque" CACHE STRING "Display name")
set(MOD_AUTHOR "Kunal" CACHE STRING "Author")
set(MOD_VERSION "0.1.0" CACHE STRING "Version")
set(MOD_LIBRARY_NAME "torque" CACHE STRING "Library name without lib/.so")

if(NOT ANDROID)
    message(FATAL_ERROR "Build with the Android NDK toolchain file.")
endif()
if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "arm64-v8a only")
endif()

# Let the linker drop unused Jolt code (applies to Jolt too, set before it).
add_compile_options(-ffunction-sections -fdata-sections)

# ---- Jolt Physics (vendored) ----------------------------------------------
add_subdirectory(third_party/JoltPhysics EXCLUDE_FROM_ALL)

# ---- LeviLauncher preloader SDK + header-only deps -------------------------
include(FetchContent)
FetchContent_Declare(preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 0.2.2
    SOURCE_SUBDIR cmake/preloader-sdk)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_Declare(boost_pfr
    GIT_REPOSITORY https://github.com/boostorg/pfr.git GIT_TAG boost-1.88.0)
FetchContent_Declare(magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git GIT_TAG v0.9.7)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.2.0)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(preloader_android nlohmann_json boost_pfr magic_enum fmt)

if(NOT TARGET preloader)
    add_subdirectory("${preloader_android_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/_preloader_android")
endif()

# ---- The mod ----------------------------------------------------------------
add_library(${MOD_LIBRARY_NAME} SHARED
    src/torque.cpp
    src/Physics.cpp
    src/UseOnProbe.cpp)
target_include_directories(${MOD_LIBRARY_NAME} PRIVATE
    src "${preloader_android_SOURCE_DIR}/include")
target_compile_options(${MOD_LIBRARY_NAME} PRIVATE
    -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(${MOD_LIBRARY_NAME} PRIVATE
    "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")

find_library(log-lib log REQUIRED)
target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE
    ${log-lib} preloader Jolt
    nlohmann_json::nlohmann_json Boost::pfr magic_enum::magic_enum fmt::fmt)

add_custom_command(TARGET ${MOD_LIBRARY_NAME} POST_BUILD
    COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${MOD_LIBRARY_NAME}>"
    VERBATIM)

# ---- .levipack --------------------------------------------------------------
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/manifest.json.in"
               "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)

set(PKG_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package")
set(PKG_DIR "${PKG_ROOT}/${MOD_ID}")
set(PKG_FILE "${CMAKE_CURRENT_BINARY_DIR}/${MOD_ID}-${MOD_VERSION}-arm64-v8a.levipack")

add_custom_target(levi_package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PKG_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PKG_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${MOD_LIBRARY_NAME}>" "${PKG_DIR}/lib${MOD_LIBRARY_NAME}.so"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" "${PKG_DIR}/manifest.json"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${PKG_ROOT}" "${CMAKE_COMMAND}" -E tar cf "${PKG_FILE}" --format=zip "${MOD_ID}"
    DEPENDS ${MOD_LIBRARY_NAME}
    VERBATIM)
