#!/usr/bin/env bash

echo "=== Linux Capture Studio 0.6.4 recording check ==="

for element in matroskamux avenc_ffv1 h264parse mp4mux avenc_aac; do
    if gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'PASS  %s\n' "$element"
    else
        printf 'MISS  %s\n' "$element"
    fi
done

if gst-inspect-1.0 x264enc >/dev/null 2>&1; then
    echo "PASS  x264enc (preferred software H.264 encoder)"
elif gst-inspect-1.0 openh264enc >/dev/null 2>&1; then
    echo "PASS  openh264enc (H.264 fallback encoder)"
else
    echo "MISS  H.264 encoder — compressed presets will fall back to FFV1"
fi
