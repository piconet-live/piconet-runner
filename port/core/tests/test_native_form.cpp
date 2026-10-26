// Tests of the native form: its bundle, the words written before the game boots, the patch list.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
// A native-form bundle: an image, the manifest that says so, and the patch list (built by port/image/build.py for
// the real game; here three words: one of the executable, two of main.rel).
std::string NativeBundle(const std::string& dir, bool complete = true, const std::string& for_image = "")
{
  std::string image(0x200, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  image.replace(0x100, 4, "FZPT");
  WriteText(dir + "/mod.bin", image);
  WriteText(dir + "/manifest.json", "{\"commit\": \"def5678\", \"form\": \"native\"}");
  const std::string sha1 = for_image.empty() ? Sha1Hex(reinterpret_cast<const u8*>(image.data()), image.size()) : for_image;
  WriteText(dir + "/patches.json", std::string("{\"complete\": ") + (complete ? "true" : "false") +
                                       ", \"image_sha1\": \"" + sha1 + "\", \"patches\": ["
                                       "{\"address\": \"801BB608\", \"original\": \"9421FFF0\", \"new\": \"49648094\", \"what\": \"mode_machine\"},"
                                       "{\"address\": \"80067898\", \"original\": \"9421FFF0\", \"new\": \"4979C5C0\", \"what\": \"sound_queue\"},"
                                       "{\"address\": \"802AEEE8\", \"original\": \"9421FFF0\", \"new\": \"49555164\", \"what\": \"cpu_seed\"}]}");
  return image;
}

// TestTheNativeForm's steps. Each has a host of its own: the core keeps the one it was last given.

void ARaceInTheWindow(TestHost& host, const std::string& dir)
{
  using namespace layout;
  // a race: no Gecko codes; the image, the mode word, the slot and the race's values are in the window before
  // the game has run a single instruction (no frame tick here)
  Options options;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &options));
  Init(host, options);
  CHECK(Active() && NativeForm() && Codes().empty() && host.log.empty());
  const std::vector<Patch>& patches = Patches();
  CHECK(patches.size() == 3 && patches[0].address == 0x80067898 && patches[1].address == 0x801BB608 &&
        patches[1].original == 0x9421FFF0 && patches[1].replacement == 0x49648094 && patches[1].what == "mode_machine");
  ModData& d = Data();
  CHECK(d.Read(MODE, 4) == 1);
  CHECK(d.Read(IMAGE, 4) == 0x465A4D44 && d.Read(SLOT, 4) == 0x465A5349 && d.Read(IMAGE + 0x100, 4) == 0x465A5054);
  CHECK(d.Read(SLOT + 4, 4) == 2 && d.Read(SLOT + 40, 4) == 0b111 && d.Read(SLOT + 44, 4) == 1);
  CHECK(d.Read(RACE, 4) == 0x465A5243 && d.Read(RACE + RACE_COURSE, 4) == 3 && d.Read(RACE + RACE_CPUSEED, 4) == 99);
  char name[NAME_SIZE] = {};
  d.CopyFrom(name, NAMES + 2 * NAME_SIZE, NAME_SIZE);
  CHECK(std::string(name) == "THREE");
  // the frame tick (the host call from the mode machine's routine) leaves the image alone and writes its words
  Guest g(host, d);
  g.Write32(net::MODULE_POINTER, MODULE);
  const std::vector<u8> window = d.Snapshot();
  FrameTick();
  CHECK(d.Read(MODE, 4) == 1 && d.Read(SLOT + 4, 4) == 2);
  std::vector<u8> after = d.Snapshot();
  std::size_t differing = 0;
  for (std::size_t i = 0; i < after.size(); ++i)
    differing += after[i] != window[i] && (WINDOW + i < CONTROL || WINDOW + i >= CONTROL + 0x1000);
  CHECK(differing == 0);   // only words of the control block (the launcher's options)
}

void TheListWrittenIntoTheCode(TestHost& host, Guest& g)
{
  // main.rel is linked: the list's words are written where the game holds the stock word, and the host is told
  for (const Patch& p : Patches())
    g.Write32(p.address, p.original);
  PatchReport r = Linked(MODULE);
  CHECK(r.written == 3 && r.already == 0 && r.refused == 0 && host.changed.size() == 3);
  CHECK(host.changed[1] == std::make_pair(u32{0x801BB608}, u32{4}));
  for (const Patch& p : Patches())
    CHECK(g.Read32(p.address) == p.replacement);
  r = Linked(MODULE);
  CHECK(r.written == 0 && r.already == 3 && host.changed.size() == 3 && host.log.empty());

  // a saved state is loaded: the stock words are back; the next picture's check writes the list again
  g.Write32(net::MODULE_POINTER, MODULE);
  EnsurePatched();
  CHECK(host.changed.size() == 3);   // nothing to do
  for (const Patch& p : Patches())
    g.Write32(p.address, p.original);
  EnsurePatched();
  CHECK(host.changed.size() == 6 && g.Read32(0x802AEEE8) == 0x49555164);
  // before main.rel is linked there is nothing to check
  g.Write32(net::MODULE_POINTER, 0);
  g.Write32(0x802AEEE8, 0x9421FFF0);
  EnsurePatched();
  CHECK(g.Read32(0x802AEEE8) == 0x9421FFF0 && host.changed.size() == 6);
}

void AWordThatIsNeitherStockNorPatched(TestHost& host, Guest& g)
{
  // a word that is neither the stock game's nor the patched one is left alone and named, once
  g.Write32(net::MODULE_POINTER, MODULE);
  g.Write32(0x802AEEE8, 0x3C601234);
  PatchReport r = Linked(MODULE);
  CHECK(r.refused == 1 && r.already == 2 && g.Read32(0x802AEEE8) == 0x3C601234 && host.changed.size() == 6);
  CHECK(host.log.find("802AEEE8") != std::string::npos && host.log.find("left alone") != std::string::npos);
  const std::size_t logged = host.log.size();
  EnsurePatched();
  CHECK(host.log.size() == logged && g.Read32(0x802AEEE8) == 0x3C601234);
  // the picture's check writes nothing at all while one word is foreign (main.rel half relocated: the words
  // with a relocation hold neither value yet), and says nothing
  g.Write32(0x801BB608, 0x9421FFF0);
  EnsurePatched();
  CHECK(g.Read32(0x801BB608) == 0x9421FFF0 && host.changed.size() == 6 && host.log.size() == logged);
  g.Write32(0x802AEEE8, 0x9421FFF0);   // relocated: every word stock or patched
  EnsurePatched();
  CHECK(g.Read32(0x801BB608) == 0x49648094 && g.Read32(0x802AEEE8) == 0x49555164 && host.changed.size() == 8);
  g.Write32(0x802AEEE8, 0x3C601234);

  // main.rel (module id 1) linked somewhere else: nothing is written, and it is said
  host.log.clear();
  g.Write32(0x801BB608, 0x9421FFF0);
  g.Write32(MODULE + 0x20, 1);
  r = Linked(MODULE + 0x20);
  CHECK(r.written == 0 && g.Read32(0x801BB608) == 0x9421FFF0 && host.log.find("801BA680") != std::string::npos);
  // another module linked by the same loader (fze.sample.rel, id 0x12, in every boot): nothing written, nothing said
  host.log.clear();
  g.Write32(MODULE + 0x20, 0x12);
  r = Linked(MODULE + 0x20);
  CHECK(r.written == 0 && g.Read32(0x801BB608) == 0x9421FFF0 && host.log.empty());
}

void APlainStart(TestHost& host, const std::string& dir)
{
  using namespace layout;
  // a plain start with the bundle: mode 0 and the image, no race
  Options options;
  options.bundle_dir = dir;
  Init(host, options);
  CHECK(!Active() && NativeForm() && Patches().size() == 3 && host.log.empty());
  CHECK(Data().Read(MODE, 4) == 0 && Data().Read(IMAGE, 4) == 0x465A4D44 && Data().Read(RACE, 4) == 0);
  CHECK(Data().Read(SLOT + 4, 4) == 0);

  Guest g(host, Data());
  TheListWrittenIntoTheCode(host, g);
  AWordThatIsNeitherStockNorPatched(host, g);
}

void AWordForced(TestHost& host, const std::string& dir)
{
  // tests: a saved state recorded with older codes at a site; forced, the word is written all the same
  Options options;
  options.bundle_dir = dir;
  options.debug.force_patches = true;
  Init(host, options);
  Guest g(host, Data());
  for (const Patch& p : Patches())
    g.Write32(p.address, p.original);
  g.Write32(0x802AEEE8, 0x3C601234);
  const PatchReport r = Linked(MODULE);
  CHECK(r.written == 3 && r.refused == 1 && g.Read32(0x802AEEE8) == 0x49555164);
}

void ABundleThatIsNotUsed(const std::string& dir, TestHost& host, TestHost& other, TestHost& large_list,
                          TestHost& large_manifest)
{
  // a list built without the game's files, or for another image, is refused: the bundle is not used
  NativeBundle(dir, false);
  Options options;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &options));
  Init(host, options);
  CHECK(!NativeForm() && !Active() && Patches().empty() && host.log.find("incomplete") != std::string::npos);
  NativeBundle(dir, true, "0000000000000000000000000000000000000000");
  Init(other, options);
  CHECK(!NativeForm() && !Active() && other.log.find("another image") != std::string::npos);
  // a number too large to be one (the parser ends the program on it) in the patch list or in the manifest: the
  // bundle is not used
  NativeBundle(dir);
  WriteText(dir + "/patches.json", "{\"complete\": true, \"words\": 1e999, \"patches\": []}");
  Init(large_list, options);
  CHECK(!NativeForm() && !Active() && large_list.log.find("not a patch list") != std::string::npos);
  NativeBundle(dir);
  WriteText(dir + "/manifest.json", "{\"commit\": \"def5678\", \"form\": \"native\", \"size\": 1e999}");
  Init(large_manifest, options);
  CHECK(!NativeForm() && !Active() && large_manifest.log.find("missing or broken") != std::string::npos);
}
}  // namespace

void TestTheNativeForm()
{
  BEGIN_TEST();
  const std::string dir = Temp();
  const std::string image = NativeBundle(dir);
  WriteText(dir + "/launch.json", R"({"about": "the native form's test race", "bundle": ")" + dir + R"(", "match": {
    "match": {"matchId": "m8", "seat": 2, "token": "1f2e3d4c5b6a7988", "relay": {"host": "", "port": 47811},
              "setup": {"course": 3, "laps": 2, "shuffleSeed": 77, "cpuSeed": 99,
                        "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "one"},
                                  {"seat": 1, "machine": 6, "playerId": "p2", "name": "two"},
                                  {"seat": 2, "machine": 7, "playerId": "p3", "name": "three"}]}},
    "relayHost": "127.0.0.1", "delay": 1}})");
  // the core keeps the host it was last given: every host of this test lives to its end
  TestHost race_host, plain_host, forced_host, incomplete_host, other, large_list, large_manifest, last_host;
  ARaceInTheWindow(race_host, dir);
  APlainStart(plain_host, dir);
  AWordForced(forced_host, dir);
  ABundleThatIsNotUsed(dir, incomplete_host, other, large_list, large_manifest);
  Options none;
  Init(last_host, none);
  CHECK(!NativeForm() && Patches().empty());
}
