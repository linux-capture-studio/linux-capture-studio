#!/usr/bin/env bash

APP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/linux-capture-studio"
DESKTOP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
METAINFO_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/metainfo"

rm -rf "$APP_HOME"
rm -f "$HOME/.local/bin/linux-capture-studio"
rm -f "$DESKTOP_HOME/io.github.linuxcapturestudio.LinuxCaptureStudio.desktop"
rm -f "$METAINFO_HOME/io.github.linuxcapturestudio.LinuxCaptureStudio.metainfo.xml"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DESKTOP_HOME" >/dev/null 2>&1 || true

echo "Removed the per-user Linux Capture Studio installation."
echo "Profiles and recordings were preserved."
