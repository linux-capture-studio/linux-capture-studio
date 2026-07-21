#!/usr/bin/env bash

FAIL=0
for CMD in meson ninja gst-launch-1.0 gst-inspect-1.0 v4l2-ctl arecord pactl udevadm; do
    if command -v "$CMD" >/dev/null 2>&1; then
        printf 'PASS  %s -> %s\n' "$CMD" "$(command -v "$CMD")"
    else
        printf 'FAIL  %s missing\n' "$CMD"
        FAIL=1
    fi
done

echo
for ELEMENT in v4l2src jpegdec jpegparse videoconvert gtk4paintablesink alsasrc audioconvert audioresample matroskamux filesink avenc_ffv1 pulsesink; do
    if gst-inspect-1.0 "$ELEMENT" >/dev/null 2>&1; then
        printf 'PASS  GStreamer element %s\n' "$ELEMENT"
    else
        printf 'FAIL  GStreamer element %s missing\n' "$ELEMENT"
        FAIL=1
    fi
done

echo
v4l2-ctl --list-devices 2>/dev/null || true
arecord -l 2>/dev/null || true
exit "$FAIL"
