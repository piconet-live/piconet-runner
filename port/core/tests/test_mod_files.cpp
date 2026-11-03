// Tests of a mod pack's files over the disc's (the launch file's "overlay", fzgx/mod_files.h): what the reader takes,
// and which files the runner takes in the offline game and in a race.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <set>

#include "fzgx/mod_files.h"

using namespace fzgx;

void TestTheModPackOverlay()
{
  // the reader: a list of {"disc", "from"}, both needed, nothing else; the file without the key has none
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && o.overlay.empty());
  CHECK(!ParseLaunchFile(R"({"about": "x", "overlay": []})", &o) && o.overlay.empty());
  CHECK(!ParseLaunchFile(R"({"about": "x", "overlay": [{"disc": "stage/st05.tpl.lz", "from": "C:\\m\\st05.tpl.lz"},
    {"disc": "bg/bg_tow.tpl.lz", "from": "C:\\m\\bg_tow.tpl.lz"}]})", &o));
  CHECK(o.overlay.size() == 2 && o.overlay[0].disc == "stage/st05.tpl.lz" && o.overlay[1].from == "C:\\m\\bg_tow.tpl.lz");
  const auto refused = [](const char* text, const char* part) {
    Options x;
    const std::optional<std::string> problem = ParseLaunchFile(text, &x);
    return problem && problem->find(part) != std::string::npos;
  };
  CHECK(refused(R"({"about": "x", "overlay": {"disc": "a"}})", "\"overlay\" is not a list"));
  CHECK(refused(R"({"about": "x", "overlay": ["a"]})", "\"overlay[0]\" is not an object"));
  CHECK(refused(R"({"about": "x", "overlay": [{"disc": "a", "from": "b"}, {"disc": "a"}]})", "\"overlay[1].from\" is missing"));
  CHECK(refused(R"({"about": "x", "overlay": [{"disc": "a", "from": "b", "size": 4}]})", "\"overlay[0].size\" is not a setting"));

  // the guard: the code's files never, in any start; the race files only in the offline game
  for (const char* code : {"main.dol", "fze.sample.rel", "FZE.MOVIE.REL", "enemy_line/line__.bin", "sys/main.dol",
                           "&&systemdata/ISO.hdr", "x/main.dol"})
  {
    CHECK(!mod_files::Refusal(code, true).empty() && !mod_files::Refusal(code, false).empty());
  }
  for (const char* race : {"game/cardata.lz", "stage/COLI_COURSE05.lz", "stage/coli_course21.lz"})
  {
    CHECK(mod_files::Refusal(race, true).empty() && !mod_files::Refusal(race, false).empty());
  }
  for (const char* look : {"stage/st05.tpl.lz", "bg/bg_tow.tpl.lz", "vehicle/gfox/gfox.tpl", "stage/st05.gma.lz"})
  {
    CHECK(mod_files::Refusal(look, true).empty() && mod_files::Refusal(look, false).empty());
  }
  for (const char* outside : {"", "/stage/st05.tpl.lz", "../sys/main.dol", "stage/../game/cardata.lz", "stage//st05",
                              "stage\\st05.tpl.lz", "C:/x", "stage/"})
  {
    CHECK(!mod_files::Refusal(outside, true).empty());
  }

  // a plan: refused entries said, junk passed over, a later entry for a path (in any case) wins in the first's place
  const std::set<std::string> disc = {"stage/st05.tpl.lz", "stage/st21.tpl.lz", "game/cardata.lz", "bg/bg_tow.tpl.lz"};
  const auto on_disc = [&](const std::string& p) { return disc.count(p) != 0 || p == "STAGE/ST05.TPL.LZ"; };
  const auto is_file = [](const std::string& f) { return f != "gone"; };
  const std::vector<OverlayFile> asked = {{"stage/st05.tpl.lz", "a"}, {"game/cardata.lz", "c"}, {"preview.png", "p"},
                                          {"bg/bg_tow.tpl.lz", "gone"}, {"stage/st21.tpl.lz", "b"},
                                          {"STAGE/ST05.TPL.LZ", "a2"}, {"fze.title.rel", "r"}};
  const mod_files::Plan race = mod_files::Choose(asked, false, on_disc, is_file);
  CHECK(race.files.size() == 2 && race.files[0].disc == "stage/st05.tpl.lz" && race.files[0].from == "a2" &&
        race.files[1].from == "b");
  CHECK(race.refused.size() == 2 && race.refused[0].find("game/cardata.lz") != std::string::npos &&
        race.refused[1].find("fze.title.rel") != std::string::npos);
  CHECK(race.not_on_disc == 1 && race.no_source == 1 && race.replaced == 1);
  const mod_files::Plan offline = mod_files::Choose(asked, true, on_disc, is_file);
  CHECK(offline.files.size() == 3 && offline.files[1].disc == "game/cardata.lz" && offline.refused.size() == 1);
  CHECK(mod_files::Summary(offline, true).find("3 files") != std::string::npos);
  CHECK(mod_files::Choose({}, false, on_disc, is_file).files.empty());
}
