#!/bin/sh
set -eu

fail() {
    printf 'watx installer: %s\n' "$*" >&2
    exit 1
}

usage() {
    printf '%s\n' \
        'Usage: sh install.sh [--version VERSION] [--prefix DIRECTORY]' \
        '' \
        'Downloads a release from theyhaveiceinthem/watx.' \
        'Default version: 0.2.0 (including its pre-release).' \
        'Default prefix: $HOME/.local' \
        'Installs bin/watx, libraries, headers, CMake files, and documentation.'
}

version=0.2.0
prefix=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || fail '--version requires a value'
            version=$2
            shift 2
            ;;
        --version=*)
            version=${1#*=}
            shift
            ;;
        --prefix)
            [ "$#" -ge 2 ] || fail '--prefix requires a value'
            [ -n "$2" ] || fail '--prefix must not be empty'
            prefix=$2
            shift 2
            ;;
        --prefix=*)
            prefix=${1#*=}
            [ -n "$prefix" ] || fail '--prefix must not be empty'
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *) fail "unknown option: $1" ;;
    esac
done

version=${version#v}
case "$version" in
    ''|*[!0-9A-Za-z.-]*) fail 'invalid release version' ;;
esac
case "$version" in
    [0-9]*) ;;
    *) fail 'release version must start with a number' ;;
esac
if [ -z "$prefix" ]; then
    [ -n "${HOME:-}" ] || fail 'set HOME or pass --prefix'
    prefix=$HOME/.local
fi
case "$prefix" in
    /*) ;;
    *) fail '--prefix must be an absolute path' ;;
esac

for tool in curl tar awk mktemp; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool"
done
if command -v sha256sum >/dev/null 2>&1; then
    hash_tool=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    hash_tool=shasum
else
    fail 'install sha256sum or shasum to verify downloads'
fi

case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform=linux ;;
    *) fail 'supported systems are macOS and Linux; use install.ps1 on Windows' ;;
esac
machine=$(uname -m)
if [ "$platform" = macos ]; then
    if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || true)" = 1 ]; then
        machine=arm64
    fi
fi
case "$machine" in
    x86_64|amd64) architecture=x86_64 ;;
    arm64|aarch64) architecture=arm64 ;;
    *) fail "unsupported CPU architecture: $machine" ;;
esac

package_name=watx-$version-$platform-$architecture
asset=$package_name.tar.gz
release_url=https://github.com/theyhaveiceinthem/watx/releases/download/v$version
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/watx-install.XXXXXXXX")
trap 'rm -rf "$temp_dir"' 0
trap 'exit 1' HUP INT TERM

download() {
    curl --fail --silent --show-error --location --retry 3 \
        --connect-timeout 20 --proto '=https' --proto-redir '=https' \
        --tlsv1.2 --output "$2" "$1" || fail "download failed: $1"
}

printf 'Downloading watx %s for %s %s...\n' "$version" "$platform" "$architecture"
download "$release_url/$asset" "$temp_dir/$asset"
download "$release_url/SHA256SUMS.txt" "$temp_dir/SHA256SUMS.txt"
expected=$(awk -v name="$asset" '$2 == name { hash = tolower($1); count++ } END { if (count != 1) exit 1; print hash }' "$temp_dir/SHA256SUMS.txt") \
    || fail 'release checksum is missing or duplicated'
case "$expected" in
    ''|*[!0-9a-f]*) fail 'invalid SHA-256 checksum' ;;
esac
[ "${#expected}" -eq 64 ] || fail 'invalid SHA-256 checksum length'
if [ "$hash_tool" = sha256sum ]; then
    actual=$(sha256sum "$temp_dir/$asset")
else
    actual=$(shasum -a 256 "$temp_dir/$asset")
fi
actual=${actual%% *}
[ "$actual" = "$expected" ] || fail 'checksum mismatch; nothing was installed'

tar -tzf "$temp_dir/$asset" > "$temp_dir/entries" || fail 'cannot read release archive'
awk -v root="$package_name" '
    { count++; if ($0 != root && index($0, root "/") != 1) exit 1;
      if ($0 ~ /(^|\/)\.\.(\/|$)/) exit 1 }
    END { if (!count) exit 1 }
' "$temp_dir/entries" || fail 'unexpected release archive layout'
mkdir "$temp_dir/unpack"
tar -xzf "$temp_dir/$asset" -C "$temp_dir/unpack" --no-same-owner
package=$temp_dir/unpack/$package_name
[ -f "$package/bin/watx" ] || fail 'release does not contain bin/watx'
[ -d "$package/include/watx" ] || fail 'release does not contain public headers'
[ -d "$package/lib/cmake/watx" ] || fail 'release does not contain CMake files'
if [ "$platform" = macos ]; then
    [ -f "$package/lib/libwatx.dylib" ] || fail 'release does not contain libwatx.dylib'
else
    [ -f "$package/lib/libwatx.so" ] || fail 'release does not contain libwatx.so'
fi
[ -f "$package/LICENSE" ] && [ -f "$package/README.md" ] || fail 'release documentation is missing'

printf 'Installing to %s...\n' "$prefix"
mkdir -p "$prefix/bin" "$prefix/lib" "$prefix/include/watx" \
    "$prefix/lib/cmake/watx" "$prefix/share/doc/watx"
cp "$package/bin/watx" "$prefix/bin/watx"
chmod 755 "$prefix/bin/watx"
for library in "$package"/lib/libwatx.*; do
    cp -P "$library" "$prefix/lib/"
done
cp -R "$package/include/watx/." "$prefix/include/watx/"
cp -R "$package/lib/cmake/watx/." "$prefix/lib/cmake/watx/"
cp "$package/LICENSE" "$package/README.md" "$prefix/share/doc/watx/"
printf '\nInstalled watx %s to %s\n' "$version" "$prefix"
case ":${PATH:-}:" in
    *":$prefix/bin:"*) printf 'Run: watx --help\n' ;;
    *) printf 'Add this directory to PATH: %s/bin\nThen run: watx --help\n' "$prefix" ;;
esac
printf 'For embedding, set CMAKE_PREFIX_PATH to %s\n' "$prefix"
