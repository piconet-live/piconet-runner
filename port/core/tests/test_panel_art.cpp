// The panel's look from the game's art (fzgx/art.h, fzgx/panel.h): a packed file unpacked, a texture's pixels, a
// picture scaled for the screen, and the load's progress.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/panel.h"

namespace
{
using namespace fzgx::panel;
namespace art = fzgx::art;

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
  // two thirds: 24 lines are 16
  CHECK(ForThePicture(native, 480, 2.0 / 3.0).height == 16 && ForThePicture(native, 480, 2.0 / 3.0).width == 91);
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
