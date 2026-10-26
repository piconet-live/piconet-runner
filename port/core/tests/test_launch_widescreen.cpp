// Tests of true widescreen as the launch file carries it (files_launch.cpp, boot.cpp's TrueAspectWord; the image's
// side: port/image/sites_view.py). Split from test_launch.cpp, the launchers' third key of the picture.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
bool Says(const std::string& text, const std::string& part)
{
  Options o;
  const std::string problem = ParseLaunchFile(text, &o).value_or("");
  if (problem.find(part) == std::string::npos)
    std::printf("refused with: \"%s\", expected: %s\n", problem.c_str(), part.c_str());
  return problem.find(part) != std::string::npos;
}
}  // namespace

// True widescreen (port/image/sites_view.py): off unless asked; on, the image's word is the game's own 16:9 aspect to
// the bit (main.rel rodata 0x49F8, 0x3FE38E39); a file that asks for it without the game's 16:9 mode is refused.
void TestTrueWidescreen()
{
  Options plain;
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &plain));
  CHECK(plain.widescreen && !plain.true_widescreen && boot::TrueAspectWord(plain) == 0);
  Options wide;
  CHECK(!ParseLaunchFile(R"({"about": "x", "trueWidescreen": true})", &wide));
  CHECK(wide.widescreen && wide.true_widescreen && boot::TrueAspectWord(wide) == 0x3FE38E39);
  wide.widescreen = false;
  CHECK(boot::TrueAspectWord(wide) == 0);
  Options off;
  CHECK(!ParseLaunchFile(R"({"about": "x", "trueWidescreen": false, "widescreen": false})", &off));
  CHECK(boot::TrueAspectWord(off) == 0);
  CHECK(Says(R"({"about": "x", "trueWidescreen": "true"})", "\"trueWidescreen\" is not true or false"));
  CHECK(Says(R"({"about": "x", "widescreen": false, "trueWidescreen": true})", "\"trueWidescreen\" is true with"));
}
