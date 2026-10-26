// Tests of the guest's memory as the core sees it, and of the core's small helpers.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

void TestGuestMemory()
{
  TestHost host;
  ModData data;
  Guest g(host, data);
  // main memory, big-endian
  g.Write32(0x80001000, 0x11223344);
  CHECK(host.ram[0x1000] == 0x11 && host.ram[0x1003] == 0x44);
  CHECK(g.Read16(0x80001002) == 0x3344 && g.Read8(0x80001001) == 0x22);
  // the last word of main memory is main memory; the next byte is nowhere
  g.Write32(0x817FFFFC, 0xA1B2C3D4);
  CHECK(g.Read32(0x817FFFFC) == 0xA1B2C3D4 && host.ram.back() == 0xD4);
  CHECK(g.Ram(0x817FFFFC, 4) != nullptr && g.Ram(0x817FFFFD, 4) == nullptr && g.Ram(0x7FFFFFFF, 1) == nullptr);
  // the mod's window: in the buffer, never in main memory, no pointer to be had
  g.Write32(layout::CONTROL + 124, 0xDEADBEEF);
  CHECK(g.Read32(layout::CONTROL + 124) == 0xDEADBEEF && data.Read(layout::CONTROL + 124, 4) == 0xDEADBEEF);
  CHECK(data.Read(layout::CONTROL + 126, 2) == 0xBEEF && data.Read(layout::CONTROL + 124, 1) == 0xDE);
  CHECK(g.Ram(layout::CONTROL, 4) == nullptr);
  data.Write(layout::INPUTS_END - 8, 0x0102030405060708ull, 8);
  CHECK(g.Read32(layout::INPUTS_END - 4) == 0x05060708);
  // outside both: reads give 0, writes go nowhere
  CHECK(!ModData::Contains(layout::WINDOW_END - 2, 4) && ModData::Contains(layout::WINDOW_END - 4, 4));
  g.Write32(layout::WINDOW_END, 5);
  CHECK(g.Read32(layout::WINDOW_END) == 0 && g.Read32(0x00000000) == 0);
  u8 six[6] = {1, 2, 3, 4, 5, 6}, back[6] = {};
  CHECK(g.CopyTo(layout::INPUTS + 8, six, 6) && g.CopyFrom(back, layout::INPUTS + 8, 6) && std::memcmp(six, back, 6) == 0);
  CHECK(!g.CopyTo(layout::WINDOW_END - 2, six, 6));
}

void TestHelpers()
{
  // base64, as a launch file carries a small picture: whole groups, one and two bytes at the end, and what is none
  CHECK(Base64Bytes("") == std::vector<u8>{});
  CHECK(Base64Bytes("TWFu") == std::vector<u8>({'M', 'a', 'n'}) && Base64Bytes("TWE=") == std::vector<u8>({'M', 'a'}));
  CHECK(Base64Bytes("TQ==") == std::vector<u8>({'M'}) && Base64Bytes("+/8A") == std::vector<u8>({0xFB, 0xFF, 0x00}));
  CHECK(!Base64Bytes("TWF") && !Base64Bytes("TW=u") && !Base64Bytes("T===") && !Base64Bytes("TWE=TWFu") && !Base64Bytes("TW-u"));
  const std::string abc = "abc";
  CHECK(Sha1Hex(reinterpret_cast<const u8*>(abc.data()), 3) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(Sha1Hex(nullptr, 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  const std::vector<u8> long_text(119, 'a');  // a length that needs the second padding block
  CHECK(Sha1Hex(long_text.data(), long_text.size()) == "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56");
  CHECK(SplitString("a:b::c", ':') == (std::vector<std::string>{"a", "b", "", "c"}));
  u64 token = 0;
  unsigned short port = 0;
  CHECK(TryParse("1f2e3d4c5b6a7988", &token, 16) && token == 0x1f2e3d4c5b6a7988ull);
  CHECK(TryParse("47811", &port) && port == 47811 && !TryParse("70000", &port) && !TryParse("12x", &port));
  Udp a, b;
  CHECK(a.Open() && b.Open() && a.LocalPort() != 0);
  const auto to = Udp::Resolve("127.0.0.1", b.LocalPort());
  CHECK(to.has_value());
  if (to)
  {
    a.Send("FZ\x03\x01", 4, *to);
    u8 buffer[16];
    std::size_t n = 0;
    Endpoint from;
    bool got = false;
    for (int i = 0; i < 2000 && !got; ++i)
      got = b.Receive(buffer, sizeof(buffer), &n, &from);
    CHECK(got && n == 4 && buffer[0] == 'F' && from.port == a.LocalPort());
    CHECK(!b.Receive(buffer, sizeof(buffer), &n, &from));
  }
}
