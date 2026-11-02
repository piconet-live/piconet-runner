// The line a race leaves in the player's file when the game stops (summary.cpp): its fields by phase from a paced
// race run to its end (paced_race.h), how the race ended, the tallies, and the file kept to its last lines.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "net_internal.h"
#include "paced_race.h"

#include <map>
#include <sstream>

namespace
{
// A summary line taken apart as its format says (protocol/formats.md, section 6): the head and the three
// phases, each a map of its key=value words; the head's first two words under "version" and "time".
std::vector<std::map<std::string, std::string>> PartsOf(const std::string& line)
{
  std::vector<std::map<std::string, std::string>> parts;
  for (std::size_t at = 0; at <= line.size();)
  {
    const std::size_t end = std::min(line.find(" | ", at), line.size());
    std::stringstream words(line.substr(at, end - at));
    parts.emplace_back();
    int bare = 0;
    for (std::string word; words >> word;)
    {
      const std::size_t equals = word.find('=');
      if (equals == std::string::npos)
        parts.back()[parts.size() == 1 ? (bare++ == 0 ? "version" : "time") : "phase"] = word;
      else
        parts.back()[word.substr(0, equals)] = word.substr(equals + 1);
    }
    at = end + 3;
  }
  return parts;
}

// "median/worst" as two numbers; {-1, -1} for "-".
std::pair<double, double> PairOf(const std::string& text)
{
  const std::size_t slash = text.find('/');
  if (slash == std::string::npos)
    return {-1, -1};
  return {std::atof(text.substr(0, slash).c_str()), std::atof(text.substr(slash + 1).c_str())};
}
}  // namespace

// A race leaves one line in the player's file when the game stops (summary.cpp): what the race was, how it ended,
// and for the countdown, the race and the time after it the ping, the frame rate, how far behind its schedule the
// game was and the frames the relay closed with another input than the player's; for the race its rollbacks and
// catch-up steps too. A paced race on a host with time to spare (the other seat's input changes every seventh
// step), run to its end and 300 frames into the results.
void TestARaceLeavesItsSummaryLine()
{
  const int failed_before = s_failed;
  Pace pace;
  pace.race_frames = 900, pace.race_to = FROM + 600;
  pace.summary = true;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.other_checksums == 0);
  // one line, ended as a line
  CHECK(!r.summary.empty() && r.summary.back() == '\n' && std::count(r.summary.begin(), r.summary.end(), '\n') == 1);
  auto parts = PartsOf(r.summary.substr(0, r.summary.size() - (r.summary.empty() ? 0 : 1)));
  CHECK(parts.size() == 4);
  if (parts.size() != 4)
    return;
  auto& head = parts[0];
  auto& race = parts[2];
  const bool head_right = head["version"] == "1" && head["time"].size() == 20 && head["time"].back() == 'Z' &&
                          head["match"] == "-" && head["players"] == "2" && head["seat"] == "0" &&
                          head["end"] == "finished" && std::atoi(head["steps"].c_str()) >= 600;
  CHECK(head_right);
  CHECK(parts[1]["phase"] == "countdown" && race["phase"] == "race" && parts[3]["phase"] == "after");
  CHECK(std::atoi(parts[1]["frames"].c_str()) == FROM);
  CHECK(std::atoi(parts[3]["frames"].c_str()) >= 250);
  // the race: its round trip (a frame of the test's, where the relay answers once a frame), its frame rate (the
  // test's host holds 60), on its schedule, its rollbacks
  const auto ping = PairOf(race["ping"]), rate = PairOf(race["fps"]), behind = PairOf(race["behind"]);
  const auto depth = PairOf(race["depth"]);
  const int frames = std::atoi(race["frames"].c_str()), rollbacks = std::atoi(race["rollbacks"].c_str());
  const bool race_right = ping.first >= 0 && ping.first <= 40 && ping.second >= ping.first && rate.first == 60 &&
                          rate.second >= 55 && behind.first >= -2 && behind.first <= 1 && behind.second >= behind.first &&
                          frames > 500 && frames <= 604 && rollbacks > 40 && rollbacks <= frames &&
                          depth.first >= 1 && depth.second >= depth.first && race.count("catchups") == 1 &&
                          race.count("held") == 1 && std::atoi(race["late"].c_str()) < frames / 10 &&
                          std::atof(race["s"].c_str()) > 8;
  CHECK(race_right);
  // the frames' own pace and the pictures': the head says how pictures are shown ("console": a console's moment,
  // unless the launch file's "present" names "field" or "copy"); the test's host holds the pace, so a displayed frame
  // follows the one before by a frame's length and few are slow; and it shows no picture, so nothing is tallied
  CHECK(head["present"] == "console" && head.count("delay") == 1);
  const auto frame = PairOf(race["frame"]);
  const bool frames_right = frame.first > 16 && frame.first < 17.5 && frame.second >= frame.first &&
                            std::atoi(race["slow"].c_str()) < frames / 4 && race["shown"] == "-" && race["uneven"] == "0" &&
                            parts[1].count("frame") == 1 && parts[3].count("shown") == 1;
  CHECK(frames_right);
  // the results run in lockstep, behind the relay: there every input of a pad that never rests is late, which is
  // why the line counts by phase (the race's own few are its start's and its last quarter second's, whose frames
  // this test's relay closes early)
  CHECK(std::atoi(parts[3]["late"].c_str()) > 250);
  // only the race has rollbacks to speak of, and the phases outside it say nothing of them
  CHECK(parts[1].count("rollbacks") == 0 && parts[3].count("rollbacks") == 0 && parts[3].count("late") == 1);
  if (s_failed != failed_before)
    std::printf("  the line: %s", r.summary.c_str());
}

namespace
{
// The file as the game did not leave it (TestTheSummarysEndsAndItsFile): too large to be the game's, one that
// cannot be read, and one of very short lines.
void TheFileAsSomebodyElseLeftIt()
{
  const std::string file = Temp() + "/race-summary.txt";
  const auto text = [&] {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  std::string why;
  // over 4 MiB: not the game's, not read, replaced
  WriteText(file, std::string((std::size_t{4} << 20) + 1, 'a'));
  CHECK(net::AppendBounded(file, "x", 5, &why) && text() == "x\n");
  // two million lines of a byte each: the last 299 and the new one, whatever stands before them
  std::string many;
  for (int i = 0; i < 2000000; ++i)
    many += i % 2 == 0 ? "a\n" : "b\n";
  WriteText(file, many);
  CHECK(net::AppendBounded(file, "z", 300, &why));
  const std::string kept = text();
  CHECK(kept.size() == 600 && std::count(kept.begin(), kept.end(), '\n') == 300 && kept.substr(0, 4) == "b\na\n" &&
        kept.substr(596) == "b\nz\n");
  // there and not to be read (not for the system's administrator, who reads any file): nothing is written, the
  // file stands as it was, and nothing is left beside it. Read as empty, its lines would have become one.
  WriteText(file, "a\nb\n");
  std::filesystem::permissions(file, std::filesystem::perms::none);
  const bool readable = std::ifstream(file).good();
  why.clear();
  const bool appended = net::AppendBounded(file, "c", 5, &why);
  std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  CHECK(readable || (!appended && !why.empty() && text() == "a\nb\n" && !std::filesystem::exists(file + ".new")));
  // a file the new text cannot be moved over (a folder of its name): said, and nothing left beside it
  const std::string taken = Temp() + "/race-summary.txt";
  std::filesystem::create_directory(taken);
  CHECK(!net::AppendBounded(taken, "x", 5, &why) && !std::filesystem::exists(taken + ".new"));
}
}  // namespace

// Official rows that come together: a race without them says none; rows that reach the game eight at a time are counted
// by size, and the other seat, whose rows differ from its prediction, is named with its burst and rollback. The local
// seat's own rows were never predicted wrong. The race stays the official one. The kept seats are the newest, oldest
// first.
void TestTheSummaryCountsBursts()
{
  Pace pace;
  pace.race_frames = 900, pace.race_to = FROM + 600;
  pace.summary = true;
  const Raced calm = RunAPacedRace(pace);
  CHECK(calm.ran && calm.other_checksums == 0);
  CHECK(calm.summary.find(" bursts=- burstseats=0 burstlast=- | after ") != std::string::npos);
  pace.rows_together = 8, pace.together_from = 100, pace.together_to = 400;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && r.other_checksums == 0);
  auto parts = PartsOf(r.summary.substr(0, r.summary.size() - (r.summary.empty() ? 0 : 1)));
  CHECK(parts.size() == 4);
  if (parts.size() != 4)
    return;
  auto& race = parts[2];
  CHECK(race["bursts"].find("8:") != std::string::npos && std::atoi(race["burstseats"].c_str()) >= 1);
  CHECK(race["burstlast"].find("/0/") == std::string::npos);
  const std::string& last = race["burstlast"];
  unsigned step = 0, rows = 0, depth = 0;
  int seat = -1;
  CHECK(std::sscanf(last.c_str(), "%u/%d/%u/%u", &step, &seat, &rows, &depth) == 4);
  CHECK(seat == 1 && rows >= net::BurstTally::LEAST_ROWS && depth >= 1 && step > FROM);
  net::BurstTally tally;
  tally.Burst(4), tally.Burst(4), tally.Burst(15), tally.Burst(16), tally.Burst(90);
  CHECK(tally.Words() == "bursts=4:2,15:1,16+:2 burstseats=0 burstlast=-");
  for (u32 i = 0; i < net::BurstTally::SEATS_KEPT + 1; ++i)
    tally.Corrected({.step = i, .seat = 2, .rows = 5, .depth = 3});
  const std::string words = tally.Words();
  CHECK(words.find(" burstseats=65 burstlast=1/2/5/3,2/2/5/3,") != std::string::npos && words.ends_with(",64/2/5/3"));
}

// How the race ended, as the line says it, and a phase nothing was counted in; the tallies' medians; and the file:
// a line a race, the last ones kept, a file that cannot be written said and left.
void TestTheSummarysEndsAndItsFile()
{
  {
    const auto session = std::make_unique<net::Session>();
    net::Session& s = *session;
    CHECK(net::SummaryLine(s, "T").find(" end=closed ") != std::string::npos);
    CHECK(net::SummaryLine(s, "T").find("| race s=0.0 frames=0 ping=- fps=- behind=- late=0 frame=- slow=0 shown=- "
                                        "uneven=0 rollbacks=0 depth=- catchups=0 held=0 bursts=- burstseats=0 burstlast=- | after ") != std::string::npos);
    CHECK(net::SummaryLine(s, "T").find(" delay=2 present=console end=closed ") != std::string::npos);
    // the pictures the host showed: counted in the phase the game is in, and only once a frame was seen; one that
    // came more than 2 ms off a frame's length after the one before is uneven, and so is a frame that long late
    s.summary.Shown(16.7);
    CHECK(s.summary.phases[net::RaceSummary::Countdown].shown.Count() == 0);
    const net::RaceSummary::Time t0{};
    s.summary.Frame(net::RaceSummary::Race, t0, 0, 10, 12);
    s.summary.Frame(net::RaceSummary::Race, t0 + std::chrono::microseconds(16700), 0, 11, 13);
    s.summary.Frame(net::RaceSummary::Race, t0 + std::chrono::microseconds(16700 + 21000), 0, 12, 14);
    for (const double ms : {16.7, 16.2, 18.5, 19.0, 14.0, 33.4})
      s.summary.Shown(ms);
    const net::PhaseTally& raced = s.summary.phases[net::RaceSummary::Race];
    CHECK(raced.shown.Count() == 6 && raced.uneven == 3 && raced.shown.Most() == 33.4 && raced.frame.Count() == 2 && raced.slow == 1);
    CHECK(net::SummaryLine(s, "T").find(" frame=16.7/21.0 slow=1 shown=16.7/33.4 uneven=3 ") != std::string::npos);
    s.settled = true;
    CHECK(net::SummaryLine(s, "T").find(" end=finished ") != std::string::npos);
    s.phase = net::Phase::Failed;
    CHECK(net::SummaryLine(s, "T").find(" end=lost ") != std::string::npos);
    s.desyncs = 1;
    CHECK(net::SummaryLine(s, "T").find(" end=desync ") != std::string::npos);
  }
  {
    net::Spread spread(-20, 0.1);
    for (const double v : {0.3, -1.2, 4.0, 0.3, 900.0})
      spread.Add(v);
    CHECK(spread.Count() == 5 && std::abs(spread.Median() - 0.3) < 1e-9 && spread.Most() == 900.0 && spread.Least() == -1.2);
    net::Spread one(0, 1);
    one.Add(5000);  // beyond the counts: the median is still a value that was added
    CHECK(one.Median() == 5000 && net::Spread(0, 1).Median() == 0);
  }
  const std::string file = Temp() + "/race-summary.txt";
  const auto text = [&] {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  std::string why;
  for (int i = 1; i <= 3; ++i)
    CHECK(net::AppendBounded(file, "line " + std::to_string(i), 5, &why));
  CHECK(text() == "line 1\nline 2\nline 3\n");
  // lines somebody's editor ended otherwise are lines all the same, empty ones are dropped; the oldest go
  WriteText(file, "a\r\nb\n\nc\nd\ne");
  CHECK(net::AppendBounded(file, "f", 5, &why) && text() == "b\nc\nd\ne\nf\n");
  CHECK(net::AppendBounded(file, "g", 1, &why) && text() == "g\n");
  CHECK(!std::filesystem::exists(file + ".new"));
  // a folder that is not there: said, nothing written
  CHECK(!net::AppendBounded(Temp() + "/none/race-summary.txt", "x", 5, &why) && !why.empty());
  TheFileAsSomebodyElseLeftIt();
}

// A report must never cost a replay. The summary is made last, after the recording is closed with its results, and
// whatever it throws ends there: a host that throws when asked for its folder leaves a recording that holds its
// results, no line, and one line of the log, which names the file and no folder. (Made before the recording was
// closed, with nothing to end a throw, the same race ended the program and left a recording without results.)
void TestASummaryThatFailsCostsNoReplay()
{
  Pace pace;
  pace.race_frames = 900, pace.race_to = FROM + 600;
  pace.summary = pace.record = true;
  const Raced whole = RunAPacedRace(pace);
  CHECK(whole.ran && !whole.results.empty() && std::count(whole.summary.begin(), whole.summary.end(), '\n') == 1);
  pace.summary_throws = true;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran);
  CHECK(!r.results.empty());
  CHECK(r.summary.empty());
  CHECK(r.warnings.find("the race's summary was not written to race-summary.txt") != std::string::npos);
  CHECK(r.warnings.find('/') == std::string::npos && r.warnings.find('\\') == std::string::npos);
}

// The pictures the host handed to the screen reach the line: the game asks its host for the times between them once
// a displayed frame (frame.cpp, TakePictureTimes) and the race's part of the line says their median and longest
// (`shown`) and how many were more than 2 ms off a frame's length (`uneven`). A host that shows a picture every
// 16,683 microseconds, and every 50th one 5 ms late. (Without the call the line read "shown=- uneven=0" for every
// player, and no test said so.)
void TestTheHostsPicturesReachTheSummaryLine()
{
  Pace pace;
  pace.race_frames = 900, pace.race_to = FROM + 600;
  pace.summary = true;
  pace.picture_us = 16683, pace.picture_late_every = 50, pace.picture_late_us = 5000;
  const Raced r = RunAPacedRace(pace);
  CHECK(r.ran && !r.summary.empty());
  auto parts = PartsOf(r.summary.substr(0, r.summary.size() - (r.summary.empty() ? 0 : 1)));
  CHECK(parts.size() == 4);
  if (parts.size() != 4)
    return;
  auto& race = parts[2];
  const auto shown = PairOf(race["shown"]);
  const int uneven = std::atoi(race["uneven"].c_str()), frames = std::atoi(race["frames"].c_str());
  const bool right = shown.first == 16.7 && shown.second == 21.7 && uneven >= frames / 50 - 1 && uneven <= frames / 50 + 1;
  CHECK(right);
  if (!right)
    std::printf("  the race's part: shown=%s uneven=%s frames=%s\n", race["shown"].c_str(), race["uneven"].c_str(),
                race["frames"].c_str());
}
