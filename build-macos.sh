#!/usr/bin/env sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
arch=$(uname -m)
deployment_target=${MACOSX_DEPLOYMENT_TARGET:-11.0}
deps_prefix="$root_dir/build/dependencies/macos-$arch/install"

sh "$root_dir/cmake/build-unix-dependencies.sh" macos "$deployment_target"
cd "$root_dir"

cmake -S . -B build/macos-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DITOOL_STATIC_THIRD_PARTY=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DwxWidgets_CONFIG_EXECUTABLE="$deps_prefix/bin/wx-config" \
    -DITOOL_CRYPTOPP_ROOT="$deps_prefix" \
    "$@"
cmake --build build/macos-release
ctest --test-dir build/macos-release --output-on-failure

artifact_name=$(cat build/macos-release/generated/artifact-name.txt)
artifact_path="build/macos-release/bin/$artifact_name.app"
if otool -L "$artifact_path/Contents/MacOS/$artifact_name" | \
   grep -E 'libwx|libcryptopp'; then
    echo "Error: iTool still has a shared wxWidgets or Crypto++ dependency." >&2
    exit 1
fi

echo "Built: $artifact_path"
