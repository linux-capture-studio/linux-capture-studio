#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_ID="io.github.linuxcapturestudio.LinuxCaptureStudio"
MANIFEST="$ROOT_DIR/$APP_ID.yml"
WORK_HOME="${XDG_CACHE_HOME:-$HOME/.cache}/linux-capture-studio-flatpak"
BUILD_DIR="$WORK_HOME/build"
REPO_DIR="$WORK_HOME/repo"
BUNDLE="$ROOT_DIR/Linux-Capture-Studio-0.6.30-RC12.flatpak"

if ! command -v flatpak >/dev/null 2>&1 || ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "Install the Flatpak build tools first:"
    echo "  sudo dnf install flatpak flatpak-builder"
    exit 1
fi

if ! flatpak remotes --columns=name 2>/dev/null | grep -qx flathub; then
    flatpak remote-add --user --if-not-exists flathub \
        https://flathub.org/repo/flathub.flatpakrepo
fi

flatpak install --user -y flathub org.gnome.Platform//50 org.gnome.Sdk//50
rm -rf "$BUILD_DIR" "$REPO_DIR" "$BUNDLE"

flatpak-builder \
    --user \
    --force-clean \
    --install-deps-from=flathub \
    --repo="$REPO_DIR" \
    "$BUILD_DIR" \
    "$MANIFEST"

flatpak build-bundle "$REPO_DIR" "$BUNDLE" "$APP_ID" stable

echo
printf 'Flatpak bundle created: %s\n' "$BUNDLE"
printf 'Install it with: flatpak install --user --reinstall %q\n' "$BUNDLE"
printf 'Run it with: flatpak run %s\n' "$APP_ID"
