#!/usr/bin/env bash

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$HOME/Downloads/opencentral-freeze-$STAMP"
ARCHIVE="$HOME/Downloads/opencentral-freeze-$STAMP.tar.gz"
DEVICE="${1:-/dev/video0}"

mkdir -p "$OUT"

{
    echo "Date: $(date --iso-8601=seconds)"
    echo "Kernel: $(uname -a)"
    echo "Device: $DEVICE"
    echo
    gst-launch-1.0 --version 2>&1 || true
} > "$OUT/system.txt"

v4l2-ctl -d "$DEVICE" --all > "$OUT/v4l2-all.txt" 2>&1 || true
v4l2-ctl -d "$DEVICE" --list-formats-ext > "$OUT/v4l2-formats.txt" 2>&1 || true
udevadm info --query=all --name="$DEVICE" > "$OUT/udev.txt" 2>&1 || true
lsusb -t > "$OUT/lsusb-tree.txt" 2>&1 || true
lsusb -v -d 07ca:0575 > "$OUT/lsusb-0575.txt" 2>&1 || true
journalctl -k --since '-10 minutes' --no-pager > "$OUT/kernel-last-10m.txt" 2>&1 || true

{
    echo "Testing five seconds at 1080p60..."
    timeout 8s gst-launch-1.0 -v \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
        ! image/jpeg,width=1920,height=1080,framerate=60/1 \
        ! fakesink sync=false 2>&1
} > "$OUT/gstreamer-1080p60.txt" || true

tar -czf "$ARCHIVE" -C "$(dirname "$OUT")" "$(basename "$OUT")"
sha256sum "$ARCHIVE"
echo "Created: $ARCHIVE"
