// A practice's countdown figure as a picture with its own transparency (panel_figure.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/panel.h"

namespace fzgx_port
{
// The figure (0 to 9) for a picture `picture_height` pixels high, straight (not premultiplied) opacity; empty where
// it cannot be drawn (no GDI+: not Windows), and the caller draws the small font instead.
fzgx::panel::Picture FigurePicture(fzgx::u32 figure, fzgx::u32 picture_height);
}  // namespace fzgx_port
