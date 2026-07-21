#!/usr/bin/env bash

DEVICE="${1:-${LINUX_CAPTURE_STUDIO_DEVICE:-/dev/video0}}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="${HOME}/Downloads/linux-capture-studio-device-${STAMP}"
ARCHIVE="${OUT}.tar.gz"
mkdir -p "$OUT"

{
    echo "Device: $DEVICE"
    echo "Date: $(date --iso-8601=seconds)"
    echo
    udevadm info --query=property --name="$DEVICE" 2>&1
} > "$OUT/udev-properties.txt"

v4l2-ctl -d "$DEVICE" --all > "$OUT/v4l2-all.txt" 2>&1 || true
v4l2-ctl -d "$DEVICE" --list-formats-ext > "$OUT/v4l2-formats.txt" 2>&1 || true
v4l2-ctl -d "$DEVICE" --list-ctrls-menus > "$OUT/v4l2-controls.txt" 2>&1 || true
lsusb -t > "$OUT/lsusb-tree.txt" 2>&1 || true
lsusb > "$OUT/lsusb.txt" 2>&1 || true
arecord -l > "$OUT/alsa-capture-hardware.txt" 2>&1 || true
arecord -L > "$OUT/alsa-capture-names.txt" 2>&1 || true
pactl list short sources > "$OUT/pipewire-sources.txt" 2>&1 || true
pactl list short sinks > "$OUT/pipewire-sinks.txt" 2>&1 || true
uname -a > "$OUT/kernel.txt" 2>&1 || true

NODE="$(basename "$(readlink -f "$DEVICE")")"
if [[ -e "/sys/class/video4linux/$NODE/device" ]]; then
    readlink -f "/sys/class/video4linux/$NODE/device" > "$OUT/sysfs-device-path.txt"
fi

tar -czf "$ARCHIVE" -C "$(dirname "$OUT")" "$(basename "$OUT")"
rm -rf "$OUT"
echo "Profile archive: $ARCHIVE"
sha256sum "$ARCHIVE"
