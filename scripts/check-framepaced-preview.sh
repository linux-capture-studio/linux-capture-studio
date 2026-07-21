#!/usr/bin/env bash

echo '=== Linux Capture Studio low-latency preview check ==='

for element in gtk4paintablesink videoconvert; do
    if gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'PASS: %s is present.
' "$element"
    else
        printf 'FAIL: %s is unavailable.
' "$element"
    fi
done

echo
echo 'The stable CPU preview is now the default.'
echo 'It uses a one-frame leaky queue and an unsynchronised GTK sink to avoid bad GC575 timestamps.'
echo
echo 'Optional GPU preview test:'
echo '  LINUX_CAPTURE_STUDIO_ENABLE_GL_PREVIEW=1 ./scripts/run-linux-capture-studio.sh'
