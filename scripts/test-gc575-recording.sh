#!/usr/bin/env bash

DEVICE="${1:-}"
FORMAT="${2:-nv12}"
RESOLUTION="${3:-1440p}"
FPS="${4:-60}"
AUDIO_DEVICE="${OPENCENTRAL_AUDIO:-hw:L21,0}"

if [[ -z "$DEVICE" ]]; then
    for NODE in /dev/v4l/by-id/*AVerMedia*video-index0 /dev/video*; do
        [[ -e "$NODE" ]] || continue
        REAL_NODE="$(readlink -f "$NODE")"
        PROPS="$(udevadm info --query=property --name="$REAL_NODE" 2>/dev/null)"
        if grep -q '^ID_VENDOR_ID=07ca$' <<<"$PROPS" && \
           grep -q '^ID_MODEL_ID=0575$' <<<"$PROPS" && \
           grep -q '^ID_V4L_CAPABILITIES=.*capture' <<<"$PROPS"; then
            DEVICE="$REAL_NODE"
            break
        fi
    done
fi

case "${RESOLUTION,,}" in
    1080|1080p|1920x1080) WIDTH=1920; HEIGHT=1080 ;;
    1440|1440p|2560x1440) WIDTH=2560; HEIGHT=1440 ;;
    4k|2160|2160p|3840x2160) WIDTH=3840; HEIGHT=2160 ;;
    *) echo "ERROR: resolution must be 1080p, 1440p, or 4k"; exit 2 ;;
esac

REQUESTED="${FORMAT,,}"
SOURCE="$REQUESTED"
SOURCE_WIDTH="$WIDTH"
SOURCE_HEIGHT="$HEIGHT"
SOURCE_FPS="$FPS"
PROCESSED_P010=0
NEEDS_SCALING=0

case "$REQUESTED:$WIDTHx$HEIGHT:$FPS" in
    nv12:1920x1080:30|nv12:1920x1080:60|nv12:1920x1080:120|\
    nv12:2560x1440:30|nv12:2560x1440:50|nv12:2560x1440:60|\
    nv12:3840x2160:30)
        SOURCE=nv12 ;;

    p010:1920x1080:30|p010:1920x1080:50|p010:1920x1080:60|\
    p010:2560x1440:30)
        SOURCE=p010 ;;

    p010:2560x1440:60|p010:3840x2160:30)
        SOURCE=nv12
        PROCESSED_P010=1
        ;;

    p010:3840x2160:60)
        SOURCE=nv12
        SOURCE_WIDTH=2560
        SOURCE_HEIGHT=1440
        SOURCE_FPS=60
        PROCESSED_P010=1
        NEEDS_SCALING=1
        ;;

    mjpeg:1920x1080:60|mjpeg:1920x1080:120|mjpeg:1920x1080:240|\
    mjpeg:2560x1440:60|mjpeg:2560x1440:120|mjpeg:2560x1440:144|\
    mjpeg:3840x2160:60)
        SOURCE=mjpeg ;;

    *)
        echo "ERROR: unsupported Linux Capture Studio mode: ${FORMAT^^} ${WIDTH}x${HEIGHT} ${FPS} FPS"
        exit 2
        ;;
esac

if [[ -z "$DEVICE" || ! -e "$DEVICE" ]]; then
    echo "ERROR: GC575 capture node not found."
    exit 1
fi

if [[ "$REQUESTED" != mjpeg ]] && ! gst-inspect-1.0 avenc_ffv1 >/dev/null 2>&1; then
    echo "ERROR: avenc_ffv1 is missing."
    echo "Install it with: sudo dnf install gstreamer1-plugin-libav"
    exit 1
fi

OUT_DIR="$HOME/Videos/Linux-Capture-Studio-tests"
mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/GC575-${REQUESTED}-${WIDTH}x${HEIGHT}-${FPS}fps-$(date +%Y%m%d-%H%M%S).mkv"

case "$SOURCE" in
    nv12)
        SOURCE_CAPS="video/x-raw,format=NV12,width=$SOURCE_WIDTH,height=$SOURCE_HEIGHT,framerate=$SOURCE_FPS/1"
        SOURCE_DECODE=()
        ;;
    p010)
        SOURCE_CAPS="video/x-raw,format=P010_10LE,width=$SOURCE_WIDTH,height=$SOURCE_HEIGHT,framerate=$SOURCE_FPS/1"
        SOURCE_DECODE=()
        ;;
    mjpeg)
        SOURCE_CAPS="image/jpeg,width=$SOURCE_WIDTH,height=$SOURCE_HEIGHT,framerate=$SOURCE_FPS/1"
        SOURCE_DECODE=(jpegdec)
        ;;
    *) echo "Internal source error"; exit 2 ;;
esac

echo "Recording 15-second validation clip:"
echo "  Requested: ${REQUESTED^^} ${WIDTH}x${HEIGHT} at ${FPS} FPS"
echo "  Native source: ${SOURCE^^} ${SOURCE_WIDTH}x${SOURCE_HEIGHT} at ${SOURCE_FPS} FPS"
if [[ "$PROCESSED_P010" -eq 1 ]]; then
    if [[ "$NEEDS_SCALING" -eq 1 ]]; then
        echo "  Processing: native ${SOURCE^^} -> Lanczos scale -> P010 working stream -> 10-bit FFV1"
    else
        echo "  Processing: native ${SOURCE^^} -> P010 working stream -> 10-bit FFV1"
    fi
fi
echo "  Audio: $AUDIO_DEVICE — 48 kHz stereo PCM"
echo "  Output: $OUT_FILE"

if [[ "$REQUESTED" == mjpeg ]]; then
    timeout --signal=INT --kill-after=8s 15s \
    gst-launch-1.0 -e \
        matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.4 \
            ! filesink location="$OUT_FILE" async=false \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
            ! "$SOURCE_CAPS" \
            ! queue max-size-time=3000000000 \
            ! jpegparse \
            ! mux. \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
            ! queue max-size-time=3000000000 \
            ! audioconvert ! audioresample \
            ! audio/x-raw,format=S16LE,rate=48000,channels=2 \
            ! mux.
elif [[ "$PROCESSED_P010" -eq 1 && "$SOURCE" == mjpeg ]]; then
    timeout --signal=INT --kill-after=20s 15s \
    gst-launch-1.0 -e \
        matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.4 \
            ! filesink location="$OUT_FILE" async=false \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
            ! "$SOURCE_CAPS" \
            ! queue max-size-time=5000000000 \
            ! jpegdec \
            ! videoconvert \
            ! video/x-raw,format=P010_10LE \
            ! videoconvert \
            ! video/x-raw,format=I420_10LE \
            ! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on \
            ! mux. \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
            ! queue max-size-time=5000000000 \
            ! audioconvert ! audioresample \
            ! audio/x-raw,format=S16LE,rate=48000,channels=2 \
            ! mux.
elif [[ "$PROCESSED_P010" -eq 1 && "$NEEDS_SCALING" -eq 1 ]]; then
    timeout --signal=INT --kill-after=25s 15s \
    gst-launch-1.0 -e \
        matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.4 \
            ! filesink location="$OUT_FILE" async=false \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
            ! "$SOURCE_CAPS" \
            ! queue max-size-time=5000000000 \
            ! videoscale method=lanczos \
            ! "video/x-raw,width=$WIDTH,height=$HEIGHT,framerate=$FPS/1" \
            ! videoconvert \
            ! video/x-raw,format=P010_10LE \
            ! videoconvert \
            ! video/x-raw,format=I420_10LE \
            ! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on \
            ! mux. \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
            ! queue max-size-time=5000000000 \
            ! audioconvert ! audioresample \
            ! audio/x-raw,format=S16LE,rate=48000,channels=2 \
            ! mux.
elif [[ "$PROCESSED_P010" -eq 1 ]]; then
    timeout --signal=INT --kill-after=20s 15s \
    gst-launch-1.0 -e \
        matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.4 \
            ! filesink location="$OUT_FILE" async=false \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
            ! "$SOURCE_CAPS" \
            ! queue max-size-time=5000000000 \
            ! videoconvert \
            ! video/x-raw,format=P010_10LE \
            ! videoconvert \
            ! video/x-raw,format=I420_10LE \
            ! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on \
            ! mux. \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
            ! queue max-size-time=5000000000 \
            ! audioconvert ! audioresample \
            ! audio/x-raw,format=S16LE,rate=48000,channels=2 \
            ! mux.
else
    ENCODE_FORMAT=I420
    [[ "$REQUESTED" == p010 ]] && ENCODE_FORMAT=I420_10LE
    timeout --signal=INT --kill-after=15s 15s \
    gst-launch-1.0 -e \
        matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.4 \
            ! filesink location="$OUT_FILE" async=false \
        v4l2src device="$DEVICE" do-timestamp=true io-mode=mmap \
            ! "$SOURCE_CAPS" \
            ! queue max-size-time=5000000000 \
            ! videoconvert \
            ! "video/x-raw,format=$ENCODE_FORMAT" \
            ! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on \
            ! mux. \
        alsasrc device="$AUDIO_DEVICE" do-timestamp=true \
            ! queue max-size-time=5000000000 \
            ! audioconvert ! audioresample \
            ! audio/x-raw,format=S16LE,rate=48000,channels=2 \
            ! mux.
fi
RC=$?

echo
if [[ -s "$OUT_FILE" ]]; then
    ls -lh "$OUT_FILE"
    file "$OUT_FILE"
    if command -v gst-discoverer-1.0 >/dev/null 2>&1; then
        gst-discoverer-1.0 "$OUT_FILE" | sed -n '1,180p'
    fi
    echo "Validation file retained: $OUT_FILE"
else
    echo "ERROR: No recording was produced. gst-launch exit code: $RC"
    exit 1
fi
