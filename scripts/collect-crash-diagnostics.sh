#!/usr/bin/env bash

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$HOME/Downloads/opencentral-crash-$STAMP"
ARCHIVE="$OUT.tar.gz"
mkdir -p "$OUT"

{
    date --iso-8601=seconds
    uname -a
    lsusb -t
    lspci -nnk | grep -A4 -Ei 'VGA|Display|Audio'
} > "$OUT/system.txt" 2>&1

{
    gst-launch-1.0 --version
    gst-inspect-1.0 gtk4paintablesink
    gst-inspect-1.0 vapostproc
    gst-inspect-1.0 videoconvertscale
} > "$OUT/gstreamer-elements.txt" 2>&1

{
    v4l2-ctl -d /dev/video0 --all
    v4l2-ctl -d /dev/video0 --list-formats-ext
} > "$OUT/gc575.txt" 2>&1

journalctl -b --no-pager -n 500 > "$OUT/journal-tail.txt" 2>&1
coredumpctl info opencentral --no-pager > "$OUT/coredump-info.txt" 2>&1 || true
coredumpctl debug opencentral --debugger-arguments='-batch -ex "thread apply all bt full"' --no-pager \
    > "$OUT/backtrace.txt" 2>&1 || true

tar -czf "$ARCHIVE" -C "$(dirname "$OUT")" "$(basename "$OUT")"
sha256sum "$ARCHIVE"
echo "Created: $ARCHIVE"
