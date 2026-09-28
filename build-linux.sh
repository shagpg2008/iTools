#!/usr/bin/env sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
arch=$(uname -m)
deps_prefix="$root_dir/build/dependencies/linux-$arch/install"

sh "$root_dir/cmake/build-unix-dependencies.sh" linux
cd "$root_dir"

cmake -S . -B build/linux-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DITOOL_STATIC_THIRD_PARTY=ON \
    -DwxWidgets_CONFIG_EXECUTABLE="$deps_prefix/bin/wx-config" \
    -DITOOL_CRYPTOPP_ROOT="$deps_prefix" \
    "$@"
cmake --build build/linux-release
ctest --test-dir build/linux-release --output-on-failure

artifact_name=$(cat build/linux-release/generated/artifact-name.txt)
artifact_path="build/linux-release/bin/$artifact_name"
ldd_output=$(ldd "$artifact_path")

if ! readelf -h "$artifact_path" | grep -q 'Type:.*EXEC'; then
    echo "Error: Linux artifact is not an ET_EXEC executable." >&2
    exit 1
fi

if printf '%s\n' "$ldd_output" | grep -E 'libwx|libcryptopp'; then
    echo "Error: iTool still has a shared wxWidgets or Crypto++ dependency." >&2
    exit 1
fi

if ! printf '%s\n' "$ldd_output" | grep -q 'libstdc++\.so'; then
    echo "Error: iTool must use the shared libstdc++ runtime on Linux." >&2
    exit 1
fi

if ! printf '%s\n' "$ldd_output" | grep -q 'libgcc_s\.so'; then
    echo "Error: iTool must use the shared libgcc unwinder on Linux." >&2
    exit 1
fi

if readelf -d "$artifact_path" | grep 'NEEDED' | grep -q 'libtiff'; then
    echo "Error: iTool has a direct dynamic libtiff dependency." >&2
    exit 1
fi

echo "Built executable: $artifact_path"
echo "Desktop launcher: build/linux-release/bin/iTool.desktop"
