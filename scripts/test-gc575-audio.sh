#!/usr/bin/env bash

AUDIO_DEVICE="${1:-hw:L21,0}"
SECONDS_TO_TEST="${2:-10}"

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "ERROR: gst-launch-1.0 was not found."
    exit 1
fi

printf 'Monitoring GC575 HDMI audio for %s seconds\n' "$SECONDS_TO_TEST"
printf 'Capture endpoint: %s\n' "$AUDIO_DEVICE"
printf 'Playback endpoint: system default\n'
printf 'Press Ctrl+C to stop early.\n\n'

timeout --signal=INT --kill-after=3s "${SECONDS_TO_TEST}s" \
    gst-launch-1.0 -q \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
        ! queue max-size-time=250000000 leaky=downstream \
        ! audioconvert ! audioresample \
        ! audio/x-raw,rate=48000,channels=2 \
        ! autoaudiosink sync=false
RC=$?

if [[ "$RC" -eq 0 || "$RC" -eq 124 || "$RC" -eq 130 ]]; then
    echo "Audio monitor test completed."
    exit 0
fi

echo "ERROR: Audio monitor failed with code $RC"
exit "$RC"
