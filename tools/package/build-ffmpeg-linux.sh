#!/usr/bin/env bash
set -euo pipefail

FFMPEG_VERSION="9.0.2"
FFMPEG_SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
FFMPEG_KEY_FPR="FCF986EA15E6E293A5644F10B4322F04D67658D8"
DAV1D_VERSION="1.5.4"
DAV1D_SHA256="686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5"
LIBVPX_VERSION="1.17.0"
LIBVPX_SHA256="1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a"

PREFIX="${BSL_DEPS_PREFIX:-$HOME/bsl-deps}"
WORK="${BSL_DEPS_WORK:-$HOME/bsl-deps-work}"
JOBS="${BSL_DEPS_JOBS:-8}"
[ "$JOBS" -gt 8 ] && JOBS=8

export PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '^/mnt/' | paste -sd:)"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"

falta=0
for f in curl gpg nasm make pkg-config sha256sum tar xz "$CC"; do
    command -v "$f" >/dev/null 2>&1 || { echo "Falta o programa: $f"; falta=1; }
done
[ "$falta" = 0 ] || exit 1

MESON=""
if command -v meson >/dev/null 2>&1; then
    MESON="meson"
elif [ -n "${BSL_MESON_PYTHONPATH:-}" ]; then
    export PYTHONPATH="$BSL_MESON_PYTHONPATH${PYTHONPATH:+:$PYTHONPATH}"
    MESON="python3 -m mesonbuild.mesonmain"
else
    echo "Falta o meson (defina BSL_MESON_PYTHONPATH apontando para uma instalacao via pip --target)."
    exit 1
fi
command -v ninja >/dev/null 2>&1 || { echo "Falta o programa: ninja"; exit 1; }

mkdir -p "$WORK/dl" "$PREFIX"
cd "$WORK/dl"

baixar() {
    local url="$1" arq="$2" sha="$3"
    [ -f "$arq" ] || curl -fsSL -o "$arq" "$url"
    echo "$sha  $arq" | sha256sum -c - >/dev/null || { echo "SHA-256 invalido: $arq"; exit 1; }
}

baixar "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" "ffmpeg-$FFMPEG_VERSION.tar.xz" "$FFMPEG_SHA256"
baixar "https://downloads.videolan.org/pub/videolan/dav1d/$DAV1D_VERSION/dav1d-$DAV1D_VERSION.tar.xz" "dav1d-$DAV1D_VERSION.tar.xz" "$DAV1D_SHA256"
baixar "https://github.com/webmproject/libvpx/archive/refs/tags/v$LIBVPX_VERSION.tar.gz" "libvpx-$LIBVPX_VERSION.tar.gz" "$LIBVPX_SHA256"

[ -f "ffmpeg-$FFMPEG_VERSION.tar.xz.asc" ] || curl -fsSL -o "ffmpeg-$FFMPEG_VERSION.tar.xz.asc" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz.asc"
[ -f ffmpeg-devel.asc ] || curl -fsSL -o ffmpeg-devel.asc https://ffmpeg.org/ffmpeg-devel.asc
GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
chmod 700 "$GNUPGHOME"
gpg --batch --quiet --import ffmpeg-devel.asc 2>/dev/null
if ! gpg --batch --status-fd 1 --verify "ffmpeg-$FFMPEG_VERSION.tar.xz.asc" "ffmpeg-$FFMPEG_VERSION.tar.xz" 2>/dev/null | grep -q "VALIDSIG $FFMPEG_KEY_FPR"; then
    echo "Assinatura do FFmpeg invalida."
    exit 1
fi
rm -rf "$GNUPGHOME"
echo "FFmpeg $FFMPEG_VERSION: SHA-256 e assinatura conferem."

cd "$WORK"
rm -rf "dav1d-$DAV1D_VERSION" "libvpx-$LIBVPX_VERSION" "ffmpeg-$FFMPEG_VERSION"
tar xf "dl/dav1d-$DAV1D_VERSION.tar.xz"
tar xf "dl/libvpx-$LIBVPX_VERSION.tar.gz"
tar xf "dl/ffmpeg-$FFMPEG_VERSION.tar.xz"

LOG="$WORK/build-deps.log"
: > "$LOG"
trap 'echo "Falha. Fim do log ($LOG):"; tail -25 "$LOG"' ERR

echo "Compilando dav1d $DAV1D_VERSION..."
$MESON setup "dav1d-$DAV1D_VERSION/build" "dav1d-$DAV1D_VERSION" \
    --prefix="$PREFIX" --libdir=lib --buildtype=release --default-library=static \
    -Db_staticpic=true -Denable_tools=false -Denable_tests=false >>"$LOG" 2>&1
ninja -C "dav1d-$DAV1D_VERSION/build" -j"$JOBS" install >>"$LOG" 2>&1

echo "Compilando libvpx $LIBVPX_VERSION..."
mkdir -p "libvpx-$LIBVPX_VERSION/build"
(
    cd "libvpx-$LIBVPX_VERSION/build"
    ../configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --enable-pic --enable-vp8 --enable-vp9 \
        --enable-vp9-highbitdepth --disable-shared --enable-static --disable-examples --disable-tools \
        --disable-docs --disable-unit-tests --as=nasm >>"$LOG" 2>&1
    make -j"$JOBS" >>"$LOG" 2>&1
    make install >>"$LOG" 2>&1
)

RPATH_ORIGEM='-Wl,-rpath,'"'"'\$\$\$\$ORIGIN'"'"
echo "Compilando FFmpeg $FFMPEG_VERSION..."
mkdir -p "ffmpeg-$FFMPEG_VERSION/build"
(
    cd "ffmpeg-$FFMPEG_VERSION/build"
    ../configure \
        --prefix="$PREFIX" \
        --enable-shared --disable-static --enable-pic \
        --disable-programs --disable-doc --disable-debug \
        --disable-avfilter --disable-autodetect \
        --enable-zlib --enable-iconv \
        --enable-libdav1d --enable-libvpx \
        --extra-ldsoflags="$RPATH_ORIGEM" \
        --cc="$CC" --cxx="$CXX" >>"$LOG" 2>&1
    make -j"$JOBS" >>"$LOG" 2>&1
    make install >>"$LOG" 2>&1
)

DOC="$PREFIX/share/doc/ffmpeg"
mkdir -p "$DOC"
FFC="$WORK/ffmpeg-$FFMPEG_VERSION"
cp "$FFC/COPYING.LGPLv2.1" "$FFC/COPYING.LGPLv3" "$FFC/LICENSE.md" "$DOC/"
cp "$WORK/dav1d-$DAV1D_VERSION/COPYING" "$DOC/dav1d-COPYING.txt"
cp "$WORK/libvpx-$LIBVPX_VERSION/LICENSE" "$DOC/libvpx-LICENSE.txt"
cp "$WORK/libvpx-$LIBVPX_VERSION/PATENTS" "$DOC/libvpx-PATENTS.txt"
{
    echo "FFmpeg $FFMPEG_VERSION (https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz)"
    echo "dav1d $DAV1D_VERSION (https://downloads.videolan.org/pub/videolan/dav1d/$DAV1D_VERSION/dav1d-$DAV1D_VERSION.tar.xz)"
    echo "libvpx $LIBVPX_VERSION (https://github.com/webmproject/libvpx/archive/refs/tags/v$LIBVPX_VERSION.tar.gz)"
    echo
    echo "Texto de configure usado:"
    sed -n 's/^configuration:[[:space:]]*//p' "$FFC/build/ffbuild/config.log" 2>/dev/null | head -1
    grep -m1 '^# ./configure\|^# \.\./configure' "$FFC/build/ffbuild/config.log" 2>/dev/null || true
    echo
    echo "Licenca resultante:"
    grep -E '^(License|--enable-gpl|--enable-version3|--enable-nonfree)' "$FFC/build/ffbuild/config.log" 2>/dev/null | head -3 || true
} > "$DOC/CONFIGURE-FFMPEG.txt"

grep -q 'enable-gpl\|enable-nonfree' "$DOC/CONFIGURE-FFMPEG.txt" && { echo "Build nao e LGPL."; exit 1; }

echo "Dependencias prontas em: $PREFIX"
echo "Use: PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig cmake ..."
