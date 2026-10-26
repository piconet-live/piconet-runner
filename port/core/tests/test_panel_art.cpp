// The panel's look from the game's art (fzgx/art.h, fzgx/panel.h): a packed file unpacked, a texture's pixels, the
// venue names' letters cut from strips made here, a picture scaled for the screen, and the load's progress.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/panel.h"

namespace
{
using namespace fzgx::panel;
namespace art = fzgx::art;

// Twelve strips made here: each letter of a venue's name a solid white block 8 wide and 20 high (an "I" 3 wide),
// 12 pixels on from the last, with a ring of half-clear grey around it.
art::Image Strips(int drop_from_strip = -1)
{
  static const char* names[STRIPS] = {"MUTECITY", "PORTTOWN", "BIGBLUE", "LIGHTNING", "SANDOCEAN", "GREENPLANT",
                                      "FIREFIELD", "CASINOPALACE", "OUTERSPACE", "AEROPOLIS", "COSMOTERMINAL", "PHANTOMROAD"};
  art::Image image{STRIP_WIDTH, STRIPS * STRIP_HEIGHT, std::vector<u8>(STRIP_WIDTH * STRIPS * STRIP_HEIGHT * 4, 0)};
  for (u32 strip = 0; strip < STRIPS; ++strip)
  {
    const std::string name = names[strip];
    for (std::size_t n = 0; n < name.size() - (static_cast<int>(strip) == drop_from_strip ? 1 : 0); ++n)
    {
      const u32 left = 20 + static_cast<u32>(n) * 12, width = name[n] == 'I' ? 3 : 8;
      for (u32 y = 6; y < 30; ++y)
        for (u32 x = left - 2; x < left + width + 2; ++x)
        {
          const bool body = y >= 8 && y < 28 && x >= left && x < left + width;
          const u8 pixel[4] = {static_cast<u8>(body ? 255 : 100), static_cast<u8>(body ? 255 : 120),
                               static_cast<u8>(body ? 255 : 140), static_cast<u8>(body ? 255 : 128)};
          std::copy_n(pixel, 4, image.At(x, strip * STRIP_HEIGHT + y));
        }
    }
  }
  return image;
}

struct Queue
{
  std::vector<u32> sizes;
};
u64 BytesOf(void* context, u32 from, u32 to)
{
  const Queue& queue = *static_cast<const Queue*>(context);
  u64 sum = 0;
  for (u32 k = from; k < to && k < queue.sizes.size(); ++k)
    sum += queue.sizes[k];
  return sum;
}
}  // namespace

// A packed file: literals and a match from the ring, both ways of counting the header; and what is refused.
void TestAPackedFileIsUnpacked()
{
  BEGIN_TEST();
  // "ABCABCABCX": three literals, a match of 6 from the ring's start place (4078 = 0xFEE), a literal
  std::vector<u8> body = {0x17, 'A', 'B', 'C', 0xEE, 0xF3, 'X'};
  const auto file = [&](u32 said, u32 size) {
    std::vector<u8> f = {u8(said), u8(said >> 8), 0, 0, u8(size), u8(size >> 8), 0, 0};
    f.insert(f.end(), body.begin(), body.end());
    return f;
  };
  const std::vector<u8> text = {'A', 'B', 'C', 'A', 'B', 'C', 'A', 'B', 'C', 'X'};
  CHECK(art::Unpack(file(7, 10)) == text);    // the header not counted
  CHECK(art::Unpack(file(15, 10)) == text);   // the header counted
  CHECK(art::Unpack(file(9, 10)).empty());    // a size that is not the file's
  CHECK(art::Unpack(file(7, 11)).empty());    // fewer bytes than it says
  CHECK(art::Unpack({}).empty() && art::Unpack({1, 2, 3}).empty());
  body.pop_back();
  body.pop_back();                            // a match cut in two
  CHECK(art::Unpack(file(5, 10)).empty());
}

// A texture file's texture: tiles of 4 by 4, both kinds of pixel; another format, a texture outside the file and
// one the file does not have give nothing.
void TestATexturesPixels()
{
  BEGIN_TEST();
  // one texture, RGB5A3, 6 by 2 (stored as two tiles), at offset 20
  std::vector<u8> file = {0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 20, 0, 6, 0, 2, 0, 1, 0x12, 0x34};
  file.resize(20 + 64, 0);
  const auto put = [&](u32 tile, u32 i, u32 value) {
    file[20 + tile * 32 + i * 2] = static_cast<u8>(value >> 8);
    file[20 + tile * 32 + i * 2 + 1] = static_cast<u8>(value);
  };
  put(0, 0, 0xFFFF);  // (0,0): solid white
  put(0, 5, 0xFC00);  // (1,1): solid red
  put(1, 1, 0x7F00);  // (5,0): red, opacity 7 of 7
  put(1, 4, 0x300F);  // (4,1): blue, opacity 3 of 7
  const art::Image image = art::Texture(file, 0);
  CHECK(image.width == 6 && image.height == 2 && image.rgba.size() == 48);
  const auto is = [&](u32 x, u32 y, u8 r, u8 g, u8 b, u8 a) {
    const u8* p = image.At(x, y);
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
  };
  CHECK(is(0, 0, 255, 255, 255, 255) && is(1, 1, 255, 0, 0, 255) && is(5, 0, 255, 0, 0, 255) && is(4, 1, 0, 0, 255, 109));
  CHECK(is(2, 0, 0, 0, 0, 0));
  CHECK(art::Texture(file, 1).Empty());
  file[7] = 14;  // another format
  CHECK(art::Texture(file, 0).Empty());
  file[7] = 5;
  file.resize(60);  // the second tile is not in the file
  CHECK(art::Texture(file, 0).Empty());
  CHECK(art::Texture({}, 0).Empty());
}

// The letters from the strips: found by their regions, never by a place written down; a strip whose regions are
// not its name's letters gives none; a line is set a pixel tighter than the letters' widths, a space 9.
void TestTheVenueLettersAreCutFromTheirStrips()
{
  BEGIN_TEST();
  Lettering none;
  CHECK(!none.Load({}) && !none.Has('A') && !none.Writes("A") && none.Line("A").Empty());
  CHECK(!none.Load(art::Image{100, 432, std::vector<u8>(100 * 432 * 4, 0)}));   // not the strips' width
  CHECK(!none.Load(art::Image{STRIP_WIDTH, 432, std::vector<u8>(STRIP_WIDTH * 432 * 4, 0)}));  // nothing in them
  Lettering letters;
  CHECK(letters.Load(Strips()));
  for (const char c : std::string("ABCDEFGHILMNOPRSTUWY"))
    CHECK(letters.Has(c));
  for (const char c : std::string("JKQVXZ0/ a"))
    CHECK(!letters.Has(c));
  CHECK(letters.Writes("LOADING") && letters.Writes("WAITING FOR PLAYERS") && !letters.Writes("3/5") && !letters.Writes(""));
  CHECK(letters.Line("3/5").Empty());
  // "LI": an L of 8 and an I of 3, each a pixel tighter, and the margins
  const art::Image li = letters.Line("LI");
  CHECK(li.height == STRIP_HEIGHT && li.width == 7 + 2 + 1 + 12);
  // solid, black outside, white in the letters, the grey ring between (128 of 255 of its colour, on black)
  const auto at = [&](const art::Image& image, u32 x, u32 y) { return image.At(x, y); };
  CHECK(at(li, 0, 0)[0] == 0 && at(li, 0, 0)[3] == 255);
  CHECK(at(li, 6, 8)[0] == 255 && at(li, 13, 27)[1] == 255);    // the L from 6, 8 wide
  CHECK(at(li, 13, 8)[0] == 255 && at(li, 15, 8)[0] == 255);    // the I from 13, 3 wide
  CHECK(at(li, 16, 8)[0] == 50 && at(li, 16, 8)[2] == 70);      // its ring
  CHECK(at(li, 5, 7)[0] == 50 && at(li, 6, 6)[1] == 60 && at(li, 6, 5)[0] == 0);
  // a space is 9 pixels
  CHECK(letters.Line("L L").width == letters.Line("LL").width + 9);
  // a strip that is not its name: MUTE CITY one region short. Its letters come from the other strips, but for Y,
  // which no other name has
  Lettering short_one;
  CHECK(short_one.Load(Strips(0)) && short_one.Has('M') && short_one.Has('U') && !short_one.Has('Y'));
  CHECK(!short_one.Writes("WAITING FOR PLAYERS") && short_one.Writes("LOADING"));
  Lettering no_w;
  CHECK(no_w.Load(Strips(1)) && !no_w.Has('W') && !no_w.Writes("WAITING FOR PLAYERS") && no_w.Writes("LOADING"));
}

// A picture of the game's size for a picture of the screen: a 480th of its height a pixel, each way, times the
// scale; on black, solid.
void TestAGamePictureScaledForTheScreen()
{
  BEGIN_TEST();
  art::Image native{136, 24, std::vector<u8>(136 * 24 * 4, 255)};
  for (u32 x = 0; x < 136; ++x)       // the top row half clear
    native.At(x, 0)[3] = 128;
  const Picture same = ForThePicture(native, 480, 1.0);
  CHECK(same.width == 136 && same.height == 24);
  // the race picture the meter was measured on: 1,344 high, the frame 381 by 67
  const Picture large = ForThePicture(native, 1344, METER_SCALE);
  CHECK(large.width == 381 && large.height == 67 && large.rgba.size() == 381u * 67 * 4);
  CHECK(large.rgba[3] == 255 && large.rgba[0] == 128 && large.rgba[(40 * 381 + 100) * 4] == 255);
  // the lettering's two thirds: a strip's 36 lines are 24, its capitals 16
  CHECK(ForThePicture(native, 480, LETTERS_SCALE).height == 16 && ForThePicture(native, 480, LETTERS_SCALE).width == 91);
  CHECK(ForThePicture({}, 480, 1.0).rgba.empty() && ForThePicture(native, 50, 1.0).rgba.empty());
  CHECK(ForThePicture(native, 480, 0.0).rgba.empty() && ForThePicture(native, 480, 9.0).rgba.empty());
}

// The load's progress: nothing before the race's mode; the first drain by its files' bytes up to three quarters;
// then two thousandths a field up to 990; never back, whatever the queue says.
void TestTheLoadsProgress()
{
  BEGIN_TEST();
  Queue queue{{0, 100, 300, 200, 400, 50, 50}};   // entry 0 is from before; the drain is entries 1 to 4: 1,000 bytes
  Progress progress;
  // the title: its own entry done, the race's queued, the mode not yet the race's
  CHECK(progress.Field(false, 5, 1, &BytesOf, &queue) == 0);
  CHECK(progress.Field(true, 5, 1, &BytesOf, &queue) == 0);
  CHECK(progress.Field(true, 5, 2, &BytesOf, &queue) == 75);     // 100 of 1,000 bytes
  CHECK(progress.Field(true, 5, 3, &BytesOf, &queue) == 300);
  CHECK(progress.Field(true, 5, 3, &BytesOf, &queue) == 300);
  CHECK(progress.Field(true, 5, 4, &BytesOf, &queue) == 450);
  // done: the rest by fields; more is queued and drained meanwhile, which changes nothing
  CHECK(progress.Field(true, 5, 5, &BytesOf, &queue) == 752);
  CHECK(progress.Field(true, 7, 5, &BytesOf, &queue) == 754);
  CHECK(progress.Field(true, 7, 6, &BytesOf, &queue) == 756);
  u32 last = 0;
  for (int field = 0; field < 300; ++field)
  {
    const u32 now = progress.Field(true, 7, 7, &BytesOf, &queue);
    CHECK(now >= last && now <= BEFORE_THE_INTRO);
    last = now;
  }
  CHECK(last == BEFORE_THE_INTRO);
  // a drain whose entries name no file goes by its entries; a queue that makes no sense shows nothing
  Queue nameless{{0, 0, 0, 0, 0}};
  Progress by_entries;
  CHECK(by_entries.Field(true, 5, 1, &BytesOf, &nameless) == 0 && by_entries.Field(true, 5, 3, &BytesOf, &nameless) == 375);
  Progress odd;
  CHECK(odd.Field(true, 3, 5, &BytesOf, &queue) == 0 && odd.Field(true, 100000, 5, &BytesOf, &queue) == 0);
  Progress idle;
  CHECK(idle.Field(true, 4, 4, &BytesOf, &queue) == 0);   // nothing queued yet
}
