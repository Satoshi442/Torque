#pragma once
// Minimal 4x4 math for editing the game's top-of-stack matrix (float[16]).
#include "Physics.h"

namespace torque {

// Memory layout of the game's matrix. The probe log decides this:
//   translation in m[12], m[13], m[14]  ->  true
//   translation in m[3],  m[7],  m[11]  ->  false
// (Both "column-major, column vectors" and "row-major, row vectors" put the
// translation at 12..14 and behave identically, so one flag covers both.)
constexpr bool kTranslationInLastRow = true;

// Logical matrix, a[row][col], column-vector convention (v' = A * v).
struct Mat4 {
  float a[4][4];
};

inline Mat4 load(const float *m) {
  Mat4 r;
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      r.a[row][col] = kTranslationInLastRow ? m[col * 4 + row] : m[row * 4 + col];
  return r;
}

inline void store(const Mat4 &r, float *m) {
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      m[kTranslationInLastRow ? col * 4 + row : row * 4 + col] = r.a[row][col];
}

inline Mat4 identity() {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    r.a[i][i] = 1.0f;
  return r;
}

inline Mat4 mul(const Mat4 &A, const Mat4 &B) {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k)
        r.a[i][j] += A.a[i][k] * B.a[k][j];
  return r;
}

inline Mat4 translation(float x, float y, float z) {
  Mat4 r = identity();
  r.a[0][3] = x;
  r.a[1][3] = y;
  r.a[2][3] = z;
  return r;
}

// Unit quaternion -> rotation matrix.
inline Mat4 rotation(const Quatf &q) {
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  Mat4 r = identity();
  r.a[0][0] = 1 - 2 * (y * y + z * z);
  r.a[0][1] = 2 * (x * y - z * w);
  r.a[0][2] = 2 * (x * z + y * w);
  r.a[1][0] = 2 * (x * y + z * w);
  r.a[1][1] = 1 - 2 * (x * x + z * z);
  r.a[1][2] = 2 * (y * z - x * w);
  r.a[2][0] = 2 * (x * z - y * w);
  r.a[2][1] = 2 * (y * z + x * w);
  r.a[2][2] = 1 - 2 * (x * x + y * y);
  return r;
}

// m = m * T(p) * R(q) * T(-p): rotate the model around pivot p (model space).
inline void applyPivotRotation(float *m, const Quatf &q, const Vec3f &p) {
  Mat4 local = mul(mul(translation(p.x, p.y, p.z), rotation(q)),
                   translation(-p.x, -p.y, -p.z));
  store(mul(load(m), local), m);
}

} // namespace torque
