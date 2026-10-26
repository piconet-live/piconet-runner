// SPDX-License-Identifier: GPL-3.0-or-later

#include "hfr_blend.h"

#include <algorithm>
#include <cmath>

namespace fzgx::hfr
{
namespace
{
struct Quat
{
  double w, x, y, z;
};

Quat Normal(const Quat& q)
{
  const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  return n > 0 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{1, 0, 0, 0};
}

Quat ToQuat(const Mtx& a)
{
  const double m00 = a.m[0], m01 = a.m[1], m02 = a.m[2], m10 = a.m[4], m11 = a.m[5], m12 = a.m[6], m20 = a.m[8],
               m21 = a.m[9], m22 = a.m[10];
  const double tr = m00 + m11 + m22;
  if (tr > 0)
  {
    const double s = std::sqrt(tr + 1.0) * 2;
    return Normal({0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s});
  }
  if (m00 > m11 && m00 > m22)
  {
    const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
    return Normal({(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s});
  }
  if (m11 > m22)
  {
    const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
    return Normal({(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s});
  }
  const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
  return Normal({(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s});
}

// The shorter arc; `angle` the whole arc.
Quat Slerp(const Quat& a, Quat b, double t, double* angle)
{
  double d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (d < 0)
  {
    b = {-b.w, -b.x, -b.y, -b.z};
    d = -d;
  }
  d = std::min(1.0, d);
  const double th = std::acos(d);
  if (angle)
    *angle = 2 * th;
  double ka = 1 - t, kb = t;
  if (th > 1e-6)
  {
    const double s = std::sin(th);
    ka = std::sin((1 - t) * th) / s;
    kb = std::sin(t * th) / s;
  }
  return Normal({ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z});
}

void SetRotation(Mtx& r, const Quat& q)
{
  const double w = q.w, x = q.x, y = q.y, z = q.z;
  r.m[0] = float(1 - 2 * (y * y + z * z));
  r.m[1] = float(2 * (x * y - z * w));
  r.m[2] = float(2 * (x * z + y * w));
  r.m[4] = float(2 * (x * y + z * w));
  r.m[5] = float(1 - 2 * (x * x + z * z));
  r.m[6] = float(2 * (y * z - x * w));
  r.m[8] = float(2 * (x * z - y * w));
  r.m[9] = float(2 * (y * z + x * w));
  r.m[10] = float(1 - 2 * (x * x + y * y));
}

// Every row of the rotation part of length 1: no scale, so a quaternion stands for it.
bool Rigid(const Mtx& a)
{
  for (int r = 0; r < 3; ++r)
  {
    const double n = double(a.m[4 * r]) * a.m[4 * r] + double(a.m[4 * r + 1]) * a.m[4 * r + 1] +
                     double(a.m[4 * r + 2]) * a.m[4 * r + 2];
    if (!(std::fabs(n - 1.0) < 2e-3))
      return false;
  }
  return true;
}

Mtx Linear(const Mtx& a, const Mtx& b, double t)
{
  Mtx r;
  for (int i = 0; i < 12; ++i)
    r.m[i] = float(a.m[i] + t * (double(b.m[i]) - a.m[i]));
  return r;
}
}  // namespace

Mtx BlendPose(const Mtx& a, const Mtx& b, double t, double* angle)
{
  if (angle)
    *angle = 0;
  if (!Rigid(a) || !Rigid(b))
    return Linear(a, b, t);
  Mtx r;
  SetRotation(r, Slerp(ToQuat(a), ToQuat(b), t, angle));
  for (int i = 3; i < 12; i += 4)
    r.m[i] = float(a.m[i] + t * (double(b.m[i]) - a.m[i]));
  return r;
}

void EyeOf(const Mtx& v, double eye[3])
{
  for (int c = 0; c < 3; ++c)
    eye[c] = -(double(v.m[c]) * v.m[3] + double(v.m[4 + c]) * v.m[7] + double(v.m[8 + c]) * v.m[11]);
}

Mtx BlendView(const Mtx& a, const Mtx& b, double t, double* angle)
{
  if (!Rigid(a) || !Rigid(b))
    return BlendPose(a, b, t, angle);
  Mtx r = Linear(a, b, t);
  SetRotation(r, Slerp(ToQuat(a), ToQuat(b), t, angle));
  double ea[3], eb[3], e[3];
  EyeOf(a, ea);
  EyeOf(b, eb);
  for (int c = 0; c < 3; ++c)
    e[c] = ea[c] + t * (eb[c] - ea[c]);
  for (int row = 0; row < 3; ++row)
    r.m[4 * row + 3] =
        float(-(double(r.m[4 * row]) * e[0] + double(r.m[4 * row + 1]) * e[1] + double(r.m[4 * row + 2]) * e[2]));
  return r;
}

Mtx Mul(const Mtx& a, const Mtx& b)
{
  Mtx r;
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 4; ++j)
    {
      double v = j == 3 ? double(a.m[4 * i + 3]) : 0.0;
      for (int k = 0; k < 3; ++k)
        v += double(a.m[4 * i + k]) * b.m[4 * k + j];
      r.m[4 * i + j] = float(v);
    }
  }
  return r;
}

Mtx Inverse(const Mtx& a)
{
  const double m00 = a.m[0], m01 = a.m[1], m02 = a.m[2], m10 = a.m[4], m11 = a.m[5], m12 = a.m[6], m20 = a.m[8],
               m21 = a.m[9], m22 = a.m[10];
  const double c00 = m11 * m22 - m12 * m21, c01 = m02 * m21 - m01 * m22, c02 = m01 * m12 - m02 * m11;
  const double c10 = m12 * m20 - m10 * m22, c11 = m00 * m22 - m02 * m20, c12 = m02 * m10 - m00 * m12;
  const double c20 = m10 * m21 - m11 * m20, c21 = m01 * m20 - m00 * m21, c22 = m00 * m11 - m01 * m10;
  const double det = m00 * c00 + m01 * c10 + m02 * c20;
  if (!(std::fabs(det) > 1e-12))
    return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
  const double k = 1.0 / det;
  const double r[9] = {c00 * k, c01 * k, c02 * k, c10 * k, c11 * k, c12 * k, c20 * k, c21 * k, c22 * k};
  Mtx o;
  for (int i = 0; i < 3; ++i)
  {
    double t = 0;
    for (int j = 0; j < 3; ++j)
    {
      o.m[4 * i + j] = float(r[3 * i + j]);
      t -= r[3 * i + j] * a.m[4 * j + 3];
    }
    o.m[4 * i + 3] = float(t);
  }
  return o;
}

Mtx CameraCorrection(const Mtx& v_old, const Mtx& v_new)
{
  return Mul(v_new, Inverse(v_old));
}

Mtx BodyCorrection(const Mtx& v_old, const Mtx& v_new, const Mtx& m_old, const Mtx& m_new)
{
  return Mul(Mul(v_new, Mul(m_new, Inverse(m_old))), Inverse(v_old));
}

Mtx BodyMotion(const Mtx& v_old, const Mtx& m_old, const Mtx& m_new)
{
  return Mul(Mul(v_old, Mul(m_new, Inverse(m_old))), Inverse(v_old));
}

double Arc(const Mtx& a, const Mtx& b)
{
  if (!Rigid(a) || !Rigid(b))
    return 0;
  double angle = 0;
  Slerp(ToQuat(a), ToQuat(b), 0.5, &angle);
  return angle;
}
}  // namespace fzgx::hfr
