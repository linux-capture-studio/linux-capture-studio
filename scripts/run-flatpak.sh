#!/usr/bin/env bash

DEVICE="${LINUX_CAPTURE_STUDIO_DEVICE:-}"

if [[ -z "$DEVICE" ]]; then
    for candidate in /dev/v4l/by-id/*video-index0 /dev/video*; do
        [[ -e "$candidate" ]] || continue
        DEVICE="$(readlink -f "$candidate")"
        break
    done
fi

[[ -n "$DEVICE" ]] || DEVICE=/dev/video0
export LINUX_CAPTURE_STUDIO_DEVICE="$DEVICE"

# Determine the USB identity without depending on host udev tools.
base="$(basename "$DEVICE")"
path="$(readlink -f "/sys/class/video4linux/$base/device" 2>/dev/null || true)"
while [[ -n "$path" && "$path" != / ]]; do
    if [[ -r "$path/idVendor" && -r "$path/idProduct" ]]; then
        export LINUX_CAPTURE_STUDIO_VENDOR_ID="$(cat "$path/idVendor" 2>/dev/null)"
        export LINUX_CAPTURE_STUDIO_MODEL_ID="$(cat "$path/idProduct" 2>/dev/null)"
        break
    fi
    path="$(dirname "$path")"
done

# Prefer a USB capture source exposed through PipeWire/PulseAudio.
if [[ -z "${LINUX_CAPTURE_STUDIO_AUDIO:-}" ]] && command -v pactl >/dev/null 2>&1; then
    AUDIO_SOURCE="$(pactl list short sources 2>/dev/null \
        | awk '$2 !~ /\.monitor$/ && $2 ~ /usb-/ {print $2; exit}')"
    if [[ -n "$AUDIO_SOURCE" ]]; then
        export LINUX_CAPTURE_STUDIO_AUDIO="pulse:$AUDIO_SOURCE"
    fi
fi

exec /app/bin/linux-capture-studio "$@"
