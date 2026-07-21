#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAILED=0

echo "=== Shell syntax ==="
while IFS= read -r script; do
    if bash -n "$script"; then
        echo "PASS: ${script#$ROOT_DIR/}"
    else
        echo "FAIL: ${script#$ROOT_DIR/}"
        FAILED=1
    fi
done < <(find "$ROOT_DIR/scripts" -maxdepth 1 -type f -name '*.sh' -print | sort)

echo
echo "=== Required files ==="
for file in LICENSE README.md CHANGELOG.md CONTRIBUTING.md SECURITY.md CODE_OF_CONDUCT.md \
            packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.desktop \
            packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.metainfo.xml; do
    if [[ -s "$ROOT_DIR/$file" ]]; then
        echo "PASS: $file"
    else
        echo "FAIL: $file"
        FAILED=1
    fi
done

echo
echo "=== Build ==="
CHECK_BUILD="$ROOT_DIR/build-release-check"
rm -rf "$CHECK_BUILD"
if meson setup "$CHECK_BUILD" "$ROOT_DIR" && meson compile -C "$CHECK_BUILD"; then
    echo "PASS: clean Meson build"
else
    echo "FAIL: clean Meson build"
    FAILED=1
fi
rm -rf "$CHECK_BUILD"

if (( FAILED != 0 )); then
    echo "Release checks failed."
    exit 1
fi

echo "All release checks passed."
