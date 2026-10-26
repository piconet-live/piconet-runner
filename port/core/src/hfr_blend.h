// The blend of the drawn frames between two steps (hfr.cpp): machine poses and camera views between two states.
// Not simulation state: nothing here needs to be the same on two hosts.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/host.h"

namespace fzgx::hfr
{
struct Mtx
{
  float m[12];  // 3 rows of 4, the translation in the fourth column (the game's Mtx)
};

// A body's pose (object to world): the rotation by the shorter arc, the translation in a straight line. A matrix
// with a scale is blended element by element. `angle`, when given: the whole arc in radians.
Mtx BlendPose(const Mtx& a, const Mtx& b, double t, double* angle = nullptr);
// A view matrix (world to camera): the rotation by the shorter arc, the eye in a straight line, the translation
// made again from both.
Mtx BlendView(const Mtx& a, const Mtx& b, double t, double* angle = nullptr);
// The rotation's arc in radians between two matrices (0 when either has a scale).
double Arc(const Mtx& a, const Mtx& b);
// A view matrix's eye: -R^T t.
void EyeOf(const Mtx& view, double eye[3]);
// a * b, as 4x4s whose last row is 0 0 0 1.
Mtx Mul(const Mtx& a, const Mtx& b);
// The inverse (any 3x3 part that has one; the identity otherwise).
Mtx Inverse(const Mtx& a);
// Where a draw at the old view `v_old` is seen from the view `v_new`: v_new * v_old^-1 (a correction applied after
// the model and view matrices). A body that moved from `m_old` to `m_new` too: v_new * m_new * m_old^-1 * v_old^-1.
Mtx CameraCorrection(const Mtx& v_old, const Mtx& v_new);
Mtx BodyCorrection(const Mtx& v_old, const Mtx& v_new, const Mtx& m_old, const Mtx& m_new);
// A body's move from `m_old` to `m_new` as the old view sees it: v_old * m_new * m_old^-1 * v_old^-1.
Mtx BodyMotion(const Mtx& v_old, const Mtx& m_old, const Mtx& m_new);
}  // namespace fzgx::hfr
