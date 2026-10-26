#!/usr/bin/env python3
"""mainrel.py LINE__.BIN OUT: main.rel as the game's loader makes it, from the disc's files/enemy_line/line__.bin.

The disc holds main.rel encrypted and packed; the loader stub (fze.sample.rel) decrypts and unpacks it at boot. The
two steps here are that loader's, as the F-Zero GX decompilation documents them (github.com/rayanht/fzgx,
tools/linecrypt.py and tools/avlz.py, CC0; the key words after CosmoCortney's Line__Crypt):
  1. every big-endian word is XORed with the next state of s' = s * KEY_MUL + KEY_ADD (mod 2^32), from KEY_START;
  2. the result is Amusement Vision's LZSS: two little-endian words (packed size, unpacked size), then flag bytes
     with eight items each, a literal byte or a two-byte reference into a 4 KiB ring that starts at 0xFEE.
The same disc gives the same bytes every time (GFZE01: SHA-1 df989873...). The output is game data: it stays in the
work folder."""
import struct, sys

KEY_ADD, KEY_MUL, KEY_START = 0x000CD8F3, 0x9B36BB94, 0xAF8910BE      # USA (GFZE01) and Europe


def decrypt(data: bytes) -> bytes:
    """XOR is its own inverse: this also encrypts."""
    count = (len(data) + 3) // 4
    words = struct.unpack(f">{count}I", data + b"\0" * (count * 4 - len(data)))
    state, out = KEY_START, []
    for word in words:
        state = (state * KEY_MUL + KEY_ADD) & 0xFFFFFFFF
        out.append(word ^ state)
    return struct.pack(f">{count}I", *out)[:len(data)]


def unpack(data: bytes) -> bytes:
    packed, size = struct.unpack_from("<II", data, 0)
    end = packed + 8
    if len(data) < end:
        raise ValueError(f"packed data cut short: the header says {end} bytes, the file has {len(data)}")
    ring, at, out, src, flags = bytearray(0x1000), 0xFEE, bytearray(), 8, 0
    while True:
        flags >>= 1
        if not flags & 0x100:
            if src >= end:
                break
            flags = data[src] | 0xFF00
            src += 1
        if flags & 1:
            if src >= end:
                break
            out.append(data[src]); ring[at] = data[src]
            src += 1; at = (at + 1) & 0xFFF
        else:
            if src + 1 >= end:
                break
            low, high = data[src], data[src + 1]
            src += 2
            where = low | ((high << 4) & 0xF00)
            for i in range((high & 0xF) + 3):
                byte = ring[(where + i) & 0xFFF]
                out.append(byte); ring[at] = byte
                at = (at + 1) & 0xFFF
    if len(out) != size:
        raise ValueError(f"unpacked {len(out)} bytes, the header says {size}")
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        rel = unpack(decrypt(f.read()))
    with open(sys.argv[2], "wb") as f:
        f.write(rel)
    print(f"main.rel: {len(rel)} bytes")
