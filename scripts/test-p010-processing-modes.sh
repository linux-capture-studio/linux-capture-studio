#!/usr/bin/env bash

DEVICE="${1:-/dev/video0}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$HOME/Downloads/opencentral-p010-processing-test-$STAMP"
ARCHIVE="$OUT.tar.gz"
mkdir -p "$OUT"

if [[ ! -e "$DEVICE" ]]; then
    echo "ERROR: $DEVICE does not exist"
    exit 1
fi

run_test() {
    local name="$1" source_width="$2" source_height="$3" source_fps="$4"
    local output_width="$5" output_height="$6" output_fps="$7"
    local log="$OUT/$name.txt"

    echo "Testing $name: NV12 ${source_width}x${source_height}@${source_fps} -> P010 ${output_width}x${output_height}@${output_fps}" | tee "$log"
    timeout 15s gst-launch-1.0 -v \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap num-buffers="$((source_fps * 5))" \
        ! "video/x-raw,format=NV12,width=$source_width,height=$source_height,framerate=$source_fps/1" \
        ! queue max-size-buffers=4 leaky=downstream \
        ! videoscale method=lanczos \
        ! "video/x-raw,width=$output_width,height=$output_height,framerate=$output_fps/1" \
        ! videoconvert \
        ! video/x-raw,format=P010_10LE \
        ! fakesink sync=false >>"$log" 2>&1
    echo "rc=$?" >>"$log"
}

{
    date --iso-8601=seconds
    lsusb -t
    v4l2-ctl -d "$DEVICE" --list-formats-ext
} > "$OUT/system-and-formats.txt" 2>&1

run_test p010-processing-1440p60 2560 1440 60 2560 1440 60
run_test p010-processing-4k30 3840 2160 30 3840 2160 30
run_test p010-processing-4k60-upscale 2560 1440 60 3840 2160 60

tar -czf "$ARCHIVE" -C "$(dirname "$OUT")" "$(basename "$OUT")"
sha256sum "$ARCHIVE"
echo "Created: $ARCHIVE"
