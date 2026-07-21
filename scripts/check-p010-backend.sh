#!/usr/bin/env bash

printf '=== Linux Capture Studio P010 processing backend ===\n'
printf 'GStreamer: '
gst-launch-1.0 --version | head -n 1

echo
MISSING=0
for ELEMENT in vaupload vapostproc vadownload; do
    if gst-inspect-1.0 "$ELEMENT" >/dev/null 2>&1; then
        echo "$ELEMENT: PRESENT"
    else
        echo "$ELEMENT: MISSING"
        MISSING=1
    fi
done

if [[ "$MISSING" -ne 0 ]]; then
    echo
    echo 'VA-API upload/postprocess/download chain is incomplete.'
    echo 'Linux Capture Studio will use the threaded CPU recording path.'
    exit 1
fi

echo
echo 'Testing NV12 -> VA upload -> scale/convert -> P010 -> download...'
timeout 15s gst-launch-1.0 -q \
    videotestsrc num-buffers=60 \
    ! video/x-raw,format=NV12,width=2560,height=1440,framerate=60/1 \
    ! vaupload \
    ! vapostproc \
    ! 'video/x-raw(memory:VAMemory),format=P010_10LE,width=3840,height=2160,framerate=60/1' \
    ! vadownload \
    ! video/x-raw,format=P010_10LE,width=3840,height=2160,framerate=60/1 \
    ! fakesink sync=false
RC=$?

echo
if [[ "$RC" -eq 0 ]]; then
    echo 'PASS: VA-API P010 scaling/conversion is usable.'
    echo 'Enable it with:'
    echo '  OPENCENTRAL_ENABLE_VA=1 ./scripts/run-opencentral.sh'
else
    echo "FAIL: VA-API test exited with code $RC."
    echo 'Linux Capture Studio will remain on the stable threaded CPU recording path.'
fi

exit "$RC"
