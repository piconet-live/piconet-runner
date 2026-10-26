#!/bin/bash
# linux-tools.sh [LB]: the tools a Linux build of the port needs, fetched into LB/tools (LB: LINUX_BUILD, default
# ~/linux-build) without root. Writes under LB, and cmake through uv under ~/.local.
#   LLVM 20.1.8       LLVM's own release for Linux (clang, lld, and the libraries the recompiler links): the
#                     version the Windows build uses (port/setup/pins.tsv, VERSION_LLVM). Its libraries are
#                     ThinLTO bitcode built against libc++, so the recompiler links with lld and libc++ (build/linux.sh)
#   ldlld/ld          a link named ld to ld.lld: on PATH first, clang links with lld (CMake's LTO check runs clang
#                     without the build's linker flags, and the system's ld cannot link bitcode)
#   cmake 3.31.6      by uv (a cmake 4 refuses some of the emulator's Externals)
#   sysroot           zlib, zstd and pkgconf from Ubuntu's packages, unpacked (apt-get download needs no root):
#                     LLVM's CMake files ask for zlib and zstd; pkg-config for the emulator's configure step
#   pkg-config        a wrapper that runs the unpacked pkgconf
# Needs the network (github.com, Ubuntu's archive), uv, and 13 GB while LLVM is unpacked (11 GB after: the release
# carries every LLVM tool and library, as measured).
set -euo pipefail
LB=$(mkdir -p "${1:-${LINUX_BUILD:-$HOME/linux-build}}" && cd "${1:-${LINUX_BUILD:-$HOME/linux-build}}" && pwd)
T=$LB/tools; mkdir -p "$T"
LLVM=LLVM-20.1.8-Linux-X64
if [ ! -x "$T/$LLVM/bin/clang" ]; then
  curl -sSL -o "$T/$LLVM.tar.xz" "https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/$LLVM.tar.xz"
  tar -xJf "$T/$LLVM.tar.xz" -C "$T" && rm "$T/$LLVM.tar.xz"
fi
mkdir -p "$T/ldlld" && ln -sf "$T/$LLVM/bin/ld.lld" "$T/ldlld/ld"
command -v uv > /dev/null || { echo "linux-tools: needs uv (https://docs.astral.sh/uv/)" >&2; exit 1; }
cmake --version 2> /dev/null | grep -q "^cmake version 3\.31\.6$" || uv tool install --force 'cmake==3.31.6'
S=$T/sysroot; A=$S/usr/lib/x86_64-linux-gnu
if [ ! -f "$A/libz.a" ] || [ ! -f "$A/libzstd.a" ] || [ ! -x "$S/usr/bin/pkgconf" ]; then
  d=$(mktemp -d); (cd "$d" && apt-get download -q zlib1g-dev libzstd-dev pkgconf pkgconf-bin libpkgconf3)
  for p in "$d"/*.deb; do dpkg -x "$p" "$S"; done; rm -rf "$d"
fi
# the packages' .so links point at the runtime libraries, which are the system's
ln -sf /usr/lib/x86_64-linux-gnu/libz.so.1 "$A/libz.so"; ln -sf /usr/lib/x86_64-linux-gnu/libzstd.so.1 "$A/libzstd.so"
cat > "$T/pkg-config" <<EOF
#!/bin/sh
LD_LIBRARY_PATH=$A exec $S/usr/bin/pkgconf --personality=$S/usr/share/pkgconfig/personality.d/x86_64-linux-gnu.personality "\$@"
EOF
chmod +x "$T/pkg-config"
echo "linux-tools: $("$T/$LLVM/bin/clang" --version | head -1); $(cmake --version | head -1); pkgconf $("$T/pkg-config" --version)"
