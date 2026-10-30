"""The game window's palette (port/moderngekko/src/overlay.h) is the design tokens every program shares: each colour of
the header's palette names a token of the protocol's theme_palette.g.h (generated from protocol/theme.tokens.json,
as the launcher's Theme.g.axaml and the site's and the service's CSS are), and types no value of its own. The
protocol's checkout is port/setup/pins.tsv's REPO_PROTOCOL."""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OVERLAY = os.path.join(ROOT, "port", "moderngekko", "src", "overlay.h")
sys.path.insert(0, os.path.join(ROOT, "port", "setup"))
import parts  # noqa: E402
PALETTE = "protocol/generated/theme_palette.g.h"   # in the protocol's checkout
# every colour of the header's palette and the token it is (the launcher's key in the comment's place)
TOKENS = {"GROUND": "GROUND", "BAR": "COLUMN", "SURFACE": "WELL", "RAISED": "RAISED", "HOVER": "HOVER",
          "ROW_LINE": "ROW_LINE", "SELECT": "SELECT", "TEXT": "TEXT", "SECONDARY": "DIM", "FAINT": "FAINT",
          "OFF_TONE": "OFF", "ACCENT": "ACCENT", "ACCENT_OVER": "ACCENT_OVER", "ACCENT_DOWN": "ACCENT_DOWN",
          "ON_ACCENT": "ON_ACCENT", "GOLD": "GOLD", "WARN": "WARN", "DANGER": "DANGER", "CLOSE_OVER": "CLOSE_OVER",
          "ON_CLOSE": "ON_CLOSE"}


def block():
    with open(OVERLAY, encoding="utf-8") as f:
        return f.read().split("// The palette, 0xRRGGBB")[1].split("\n\n")[0]


def palette():
    """The header's palette: its block's `constexpr std::uint32_t NAME = fzgx::theme::TOKEN` (several to a line too)."""
    return dict(re.findall(r"\b([A-Z_]+) = fzgx::theme::([A-Z_0-9]+)\b", block()))


def generated():
    """The generated palette's tokens: `constexpr std::uint32_t TOKEN = 0xRRGGBB;`."""
    with open(parts.path_in("REPO_PROTOCOL", PALETTE), encoding="utf-8") as f:
        return dict(re.findall(r"\b([A-Z_0-9]+) = 0x([0-9A-F]{6});", f.read()))


class OverlayPaletteTest(unittest.TestCase):
    def test_every_colour_of_the_window_is_a_design_token(self):
        ours, tokens = palette(), generated()
        self.assertEqual(ours, TOKENS, "overlay.h's palette and this test's list of tokens")
        for name, token in TOKENS.items():
            with self.subTest(name=name):
                self.assertIn(token, tokens, f"{PALETTE} has no colour {token}")

    def test_the_palette_types_no_colour_of_its_own(self):
        self.assertNotRegex(block(), r"0x[0-9A-Fa-f]{6}")


if __name__ == "__main__":
    unittest.main()
