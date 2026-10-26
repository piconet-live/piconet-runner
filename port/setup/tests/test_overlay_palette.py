"""The game window's palette (port/moderngekko/src/overlay.h) is the launcher's theme, value for value: the header is
a copy by hand of net/src/Launcher.App/Theme/Theme.axaml (the two programs share no file), and this test holds every colour of the header's palette to the theme's key it copies. The
launcher's checkout is port/setup/pins.tsv's REPO_LAUNCHER, so the test reads it wherever the launcher is."""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OVERLAY = os.path.join(ROOT, "port", "moderngekko", "src", "overlay.h")
sys.path.insert(0, os.path.join(ROOT, "port", "setup"))
import parts  # noqa: E402
THEME = "net/src/Launcher.App/Theme/Theme.axaml"   # in the launcher's checkout
# every colour of the header's palette and the theme's key it is (a Color or a SolidColorBrush's Color)
KEYS = {"GROUND": "GroundColor", "BAR": "ColumnColor", "SURFACE": "WellColor", "RAISED": "RaisedColor",
        "HOVER": "HoverColor", "ROW_LINE": "RowLineBrush", "SELECT": "SelectBrush", "TEXT": "TextColor",
        "SECONDARY": "DimColor", "FAINT": "FaintColor", "OFF_TONE": "OffColor", "ACCENT": "AccentColor",
        "ACCENT_OVER": "AccentOverBrush", "ACCENT_DOWN": "AccentDownBrush", "ON_ACCENT": "OnAccentColor",
        "GOLD": "GoldColor", "WARN": "WarnColor", "DANGER": "DangerColor", "CLOSE_OVER": "CloseOverBrush",
        "ON_CLOSE": "OnCloseBrush"}


def palette():
    """The header's palette: its block's `constexpr std::uint32_t NAME = 0xRRGGBB` (several to a line too)."""
    with open(OVERLAY, encoding="utf-8") as f:
        block = f.read().split("// The palette, 0xRRGGBB")[1].split("\n\n")[0]
    return {name: int(value, 16) for name, value in re.findall(r"\b([A-Z_]+) = 0x([0-9A-Fa-f]{6})\b", block)}


def theme():
    """The theme's colours by key: `<Color x:Key="K">#RRGGBB</Color>` and `<SolidColorBrush x:Key="K" Color="#RRGGBB" />`."""
    with open(parts.path_in("REPO_LAUNCHER", THEME), encoding="utf-8") as f:
        text = f.read()
    found = dict(re.findall(r'<Color x:Key="(\w+)">#([0-9A-Fa-f]{6})</Color>', text))
    found.update(re.findall(r'<SolidColorBrush x:Key="(\w+)" Color="#([0-9A-Fa-f]{6})"', text))
    return {key: int(value, 16) for key, value in found.items()}


class OverlayPaletteTest(unittest.TestCase):
    def test_every_colour_of_the_window_is_the_launchers(self):
        ours, theirs = palette(), theme()
        self.assertEqual(sorted(ours), sorted(KEYS), "overlay.h's palette and this test's list of keys")
        for name, key in KEYS.items():
            with self.subTest(name=name, key=key):
                self.assertIn(key, theirs, f"{THEME} has no colour {key}")
                self.assertEqual(f"{ours[name]:06X}", f"{theirs[key]:06X}", f"overlay.h's {name} is not {key}")


if __name__ == "__main__":
    unittest.main()
