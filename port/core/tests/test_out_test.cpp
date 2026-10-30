// Whether a machine is out (out_test.h, the game's own verdict): held to the same cases as the image's text and its
// Python model (port/image/tests/out-cases.tsv, read by test_out_test.py too), and read from a game's memory.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "out_test.h"
#include "util.h"

// Every case of the shared list: the verdict the list says, from the bits as the list gives them.
void TestTheOutTestsCases()
{
  BEGIN_TEST();
  std::ifstream file(std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "image" / "tests" / "out-cases.tsv");
  CHECK(file.good());
  u32 cases = 0, outs = 0, wrong = 0;
  std::string line;
  std::getline(file, line);   // the columns' names
  while (std::getline(file, line))
  {
    const std::vector<std::string> cells = SplitString(line, '\t');
    u32 flags = 0, contact = 0, speed = 0, rules = 0, race_type = 0;
    if (cells.size() != 7 || !TryParse(cells[0], &flags, 16) || !TryParse(cells[1], &contact, 16) ||
        !TryParse(cells[2], &speed, 16) || !TryParse(cells[3], &rules, 16) || !TryParse(cells[4], &race_type) ||
        race_type > 0xFF || (cells[5] != "out" && cells[5] != "racing"))
    {
      ++wrong;
      std::printf("  not a case: %s\n", line.c_str());
      continue;
    }
    const bool out = net::out_test::Out(flags, contact, speed, rules, static_cast<u8>(race_type));
    ++cases;
    outs += out;
    if (out != (cells[5] == "out"))
    {
      ++wrong;
      std::printf("  %s: the list says %s, out_test.h %s\n", cells[6].c_str(), cells[5].c_str(),
                  out ? "out" : "racing");
    }
  }
  CHECK(wrong == 0);
  CHECK(cases >= 30 && outs > 10 && outs < cases);
}

// The reader: the machine's flags, +0x58C and +0x17C from its block, the rule word and the race type from main.rel's
// .bss by the module pointer's value; it writes nothing.
void TestTheOutTestReadsTheGame()
{
  BEGIN_TEST();
  TestHost host;
  ModData data;
  Guest g(host, data);
  LinkMainRel(g);
  constexpr u32 machine = 0x80400000;
  const auto out = [&] { return net::out_test::Out(g, MODULE, machine); };
  CHECK(!out());
  g.Write32(machine, 0x80);                                       // a breakdown, at rest
  CHECK(out());
  g.Write32(machine + game::MACHINE_SPEED, std::bit_cast<u32>(50.0f));          // ... moving
  CHECK(!out());
  g.Write8(MODULE + net::BSS + 0x3C35, 5);                        // ... in race type 5
  CHECK(out());
  g.Write32(MODULE + net::BSS + 0x3C30, 0x04008000);              // ... under Restore
  CHECK(!out());
  g.Write32(MODULE + net::BSS + 0x3C30, 0x04000000);
  g.Write8(MODULE + net::BSS + 0x3C35, 6);
  g.Write32(machine + game::MACHINE_FLAGS2, 0x10);                               // a fall
  CHECK(out());
  g.Write32(machine, 0x10080);                                    // finished
  CHECK(!out());
  const std::vector<u8, PageMemory<u8>> before = host.ram;
  CHECK(!out() && host.ram == before);
}
