#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/linux-capture-studio"
BIN_HOME="$HOME/.local/bin"
DESKTOP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
METAINFO_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/metainfo"
ICON_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/512x512/apps"

if [[ ! -x "$ROOT_DIR/build/src/linux-capture-studio" ]]; then
    echo "Linux Capture Studio is not built; building it now..."
    rm -rf "$ROOT_DIR/build"
    meson setup "$ROOT_DIR/build" "$ROOT_DIR" || exit 1
    meson compile -C "$ROOT_DIR/build" || exit 1
fi

STAGE="${APP_HOME}.new"
rm -rf "$STAGE"
mkdir -p "$STAGE/scripts" "$STAGE/build/src" "$BIN_HOME" "$DESKTOP_HOME" "$METAINFO_HOME" "$ICON_HOME"

cp -a "$ROOT_DIR/scripts/." "$STAGE/scripts/"
cp -a "$ROOT_DIR/docs" "$STAGE/"
cp -a "$ROOT_DIR/README.md" "$ROOT_DIR/LICENSE" "$ROOT_DIR/RELEASE-NOTES.txt" "$STAGE/"
cp -a "$ROOT_DIR/build/src/linux-capture-studio" "$STAGE/build/src/"
if [[ -x "$ROOT_DIR/build/src/linux-capture-studio-wait" ]]; then
    cp -a "$ROOT_DIR/build/src/linux-capture-studio-wait" "$STAGE/build/src/"
fi

cat > "$BIN_HOME/linux-capture-studio" <<EOF
#!/usr/bin/env bash
exec "$APP_HOME/scripts/run-linux-capture-studio.sh" "\$@"
EOF
chmod 0755 "$BIN_HOME/linux-capture-studio"

rm -rf "$APP_HOME"
mv "$STAGE" "$APP_HOME"

cp -f "$ROOT_DIR/packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.desktop" \
    "$DESKTOP_HOME/io.github.linuxcapturestudio.LinuxCaptureStudio.desktop"
cp -f "$ROOT_DIR/packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.metainfo.xml" \
    "$METAINFO_HOME/io.github.linuxcapturestudio.LinuxCaptureStudio.metainfo.xml"
cp -f "$ROOT_DIR/packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.png" \
    "$ICON_HOME/io.github.linuxcapturestudio.LinuxCaptureStudio.png"

command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -f -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" \
    >/dev/null 2>&1 || true

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DESKTOP_HOME" >/dev/null 2>&1 || true

echo "Installed Linux Capture Studio for the current user."
echo "Launch it from the application menu or run: linux-capture-studio"
echo "No sudo privileges were used."
