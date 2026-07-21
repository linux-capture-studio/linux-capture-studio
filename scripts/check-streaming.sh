#!/usr/bin/env bash

echo "=== Linux Capture Studio 0.6.4 streaming check ==="
missing=0

for element in flvmux h264parse avenc_aac; do
    if gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'PASS  %s\n' "$element"
    else
        printf 'MISS  %s\n' "$element"
        missing=1
    fi
done

if gst-inspect-1.0 rtmp2sink >/dev/null 2>&1; then
    echo "PASS  rtmp2sink (preferred RTMP/RTMPS sink)"
elif gst-inspect-1.0 rtmpsink >/dev/null 2>&1; then
    echo "PASS  rtmpsink (compatibility RTMP/RTMPS sink)"
else
    echo "MISS  rtmp2sink or rtmpsink"
    missing=1
fi

if gst-inspect-1.0 x264enc >/dev/null 2>&1; then
    echo "PASS  x264enc"
elif gst-inspect-1.0 openh264enc >/dev/null 2>&1; then
    echo "PASS  openh264enc"
else
    echo "MISS  x264enc or openh264enc"
    missing=1
fi

if [[ "$missing" -eq 0 ]]; then
    echo "READY Streaming dependencies are present."
else
    echo "NOT READY Install the missing GStreamer plug-ins before streaming."
fi
exit "$missing"
