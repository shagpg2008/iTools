#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 linux|macos [macOS deployment target]" >&2
    exit 2
fi

platform=$1
deployment_target=${2:-}
case "$platform" in
    linux|macos) ;;
    *) echo "Unsupported platform: $platform" >&2; exit 2 ;;
esac

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=$(uname -m)
wx_version=3.2.11
cryptopp_version=8.9.0
wx_archive="$root_dir/src/third_party/distfiles/wxWidgets-3.2.11.zip"
cryptopp_archive="$root_dir/src/third_party/distfiles/cryptopp890.zip"
wx_sha256=02C4FDC8EC104A10EFD809238F800B632C4D5CC6A2D54582BFF775240007F01A
cryptopp_sha256=4CC0CCC324625B80B695FCD3DEE63A66F1A460D3E51B71640CDBFC4CD1A3779C

deps_root="$root_dir/build/dependencies/$platform-$arch"
source_root="$deps_root/source"
build_root="$deps_root/build"
install_root="$deps_root/install"
stamp_root="$deps_root/stamps"

jobs=${ITOOL_BUILD_JOBS:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
fi

require_file()
{
    if [ ! -f "$1" ]; then
        echo "Missing dependency archive: $1" >&2
        exit 1
    fi
}

verify_archive()
{
    archive=$1
    expected=$2
    actual=$(cmake -E sha256sum "$archive")
    actual=${actual%% *}
    # Digest text is case-insensitive. CMake emits lowercase on Unix while
    # hashes recorded by other platform tools are commonly uppercase.
    actual=$(printf '%s' "$actual" | tr '[:upper:]' '[:lower:]')
    expected=$(printf '%s' "$expected" | tr '[:upper:]' '[:lower:]')
    if [ "$actual" != "$expected" ]; then
        echo "SHA256 mismatch: $archive" >&2
        echo "Expected: $expected" >&2
        echo "Actual:   $actual" >&2
        exit 1
    fi
}

extract_archive()
{
    archive=$1
    destination=$2
    stamp=$3
    signature=$4
    if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$signature" ]; then
        return
    fi
    rm -rf "$destination"
    mkdir -p "$destination" "$(dirname "$stamp")"
    (cd "$destination" && cmake -E tar xf "$archive")
    printf '%s\n' "$signature" > "$stamp"
}

require_file "$wx_archive"
require_file "$cryptopp_archive"
verify_archive "$wx_archive" "$wx_sha256"
verify_archive "$cryptopp_archive" "$cryptopp_sha256"

mkdir -p "$source_root" "$build_root" "$install_root" "$stamp_root"
extract_archive "$wx_archive" "$source_root/wxWidgets" \
    "$stamp_root/wx-source" "$wx_version-$wx_sha256"
extract_archive "$cryptopp_archive" "$source_root/cryptopp" \
    "$stamp_root/cryptopp-source" "$cryptopp_version-$cryptopp_sha256"

wx_build="$build_root/wxWidgets"
wx_stamp="$stamp_root/wx-installed"
wx_signature="$wx_version-$wx_sha256-$platform-$arch-$deployment_target-bundled-image-libs-v1"
if [ ! -f "$wx_stamp" ] || [ "$(cat "$wx_stamp")" != "$wx_signature" ]; then
    rm -rf "$wx_build" "$install_root"
    rm -f "$stamp_root/cryptopp-installed"
    mkdir -p "$wx_build" "$install_root"
    set -- \
        -S "$source_root/wxWidgets" \
        -B "$wx_build" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$install_root" \
        -DwxBUILD_SHARED=OFF \
        -DwxBUILD_MONOLITHIC=OFF \
        -DwxBUILD_SAMPLES=OFF \
        -DwxBUILD_TESTS=OFF \
        -DwxBUILD_DEMOS=OFF \
        -DwxUSE_WEBVIEW=OFF \
        -DwxUSE_ZLIB=builtin \
        -DwxUSE_EXPAT=builtin \
        -DwxUSE_LIBJPEG=builtin \
        -DwxUSE_LIBPNG=builtin \
        -DwxUSE_LIBTIFF=builtin
    if [ "$platform" = macos ]; then
        set -- "$@" -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
    fi
    cmake "$@"
    cmake --build "$wx_build" --parallel "$jobs"
    cmake --install "$wx_build"
    printf '%s\n' "$wx_signature" > "$wx_stamp"
fi

cryptopp_stamp="$stamp_root/cryptopp-installed"
cryptopp_signature="$cryptopp_version-$cryptopp_sha256-$platform-$arch-$deployment_target"
if [ ! -f "$cryptopp_stamp" ] || [ "$(cat "$cryptopp_stamp")" != "$cryptopp_signature" ]; then
    # Crypto++ uses an in-source GNUmakefile, so remove objects from any
    # interrupted or differently configured build before rebuilding.
    make -C "$source_root/cryptopp" clean >/dev/null 2>&1 || true
    if [ "$platform" = macos ]; then
        make -C "$source_root/cryptopp" -j "$jobs" static \
            CXXFLAGS="-DNDEBUG -O2 -mmacosx-version-min=$deployment_target"
        make -C "$source_root/cryptopp" install-lib \
            PREFIX="$install_root"
    else
        make -C "$source_root/cryptopp" -j "$jobs" static
        make -C "$source_root/cryptopp" install-lib \
            PREFIX="$install_root"
    fi
    printf '%s\n' "$cryptopp_signature" > "$cryptopp_stamp"
fi

echo "Static dependencies ready: $install_root"
