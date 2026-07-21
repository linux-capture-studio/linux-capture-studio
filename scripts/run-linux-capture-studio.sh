#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE="${LINUX_CAPTURE_STUDIO_DEVICE:-}"
[[ -n "${1:-}" ]] && DEVICE="$1"

STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/linux-capture-studio"
STATE_FILE="$STATE_DIR/gc575-xhci-bdf"
SESSION_FILE="$STATE_DIR/session-mode"
PENDING_SESSION_FILE="$STATE_DIR/pending-session-mode"
TRANSITION_STATUS_FILE="$STATE_DIR/transition-status"
TRANSITION_WAIT_BIN="$ROOT_DIR/build/src/linux-capture-studio-wait"
OLD_STATE_FILE="${XDG_STATE_HOME:-$HOME/.local/state}/opencentral/gc575-xhci-bdf"

SESSION_MODE="${LINUX_CAPTURE_STUDIO_SESSION_MODE:-}"
if [[ -z "$SESSION_MODE" && -r "$SESSION_FILE" ]]; then
    SESSION_MODE="$(head -n1 "$SESSION_FILE" 2>/dev/null)"
fi
[[ -n "$SESSION_MODE" ]] || SESSION_MODE="smooth"

update_transition_status() {
    local message="$1" tmp
    mkdir -p "$STATE_DIR"
    tmp="$TRANSITION_STATUS_FILE.tmp.$$"
    printf '%s\n' "$message" > "$tmp"
    mv -f "$tmp" "$TRANSITION_STATUS_FILE"
}

apply_session_mode() {
    case "$SESSION_MODE" in
        hdr1080)
            NATIVE_P010=1
            NATIVE_HDR_MODE=1080p60
            HDR60_OUTPUT=1080p60
            ;;
        hdr1440)
            NATIVE_P010=1
            NATIVE_HDR_MODE=1440p60
            HDR60_OUTPUT=1440p60
            ;;
        hdr4k)
            NATIVE_P010=1
            NATIVE_HDR_MODE=2160p60
            HDR60_OUTPUT=2160p60
            ;;
        *)
            SESSION_MODE=smooth
            NATIVE_P010=0
            NATIVE_HDR_MODE=1080p60
            HDR60_OUTPUT=
            ;;
    esac
}

remember_session_mode() {
    mkdir -p "$STATE_DIR"
    printf '%s\n' "$SESSION_MODE" > "$SESSION_FILE"
}

apply_session_mode

if [[ ! -f "$STATE_FILE" && -f "$OLD_STATE_FILE" ]]; then
    mkdir -p "$STATE_DIR"
    cp -f "$OLD_STATE_FILE" "$STATE_FILE" 2>/dev/null || true
fi

node_properties() {
    udevadm info --query=property --name="$1" 2>/dev/null
}

property_value() {
    local props="$1" key="$2"
    sed -n "s/^${key}=//p" <<<"$props" | head -n1
}

is_capture_node() {
    local node="$1" props caps
    [[ -e "$node" ]] || return 1
    props="$(node_properties "$node")"
    caps="$(property_value "$props" ID_V4L_CAPABILITIES)"
    if [[ "$caps" == *capture* ]]; then
        return 0
    fi
    python3 - "$node" <<'PY' >/dev/null 2>&1
import fcntl, os, struct, sys
node=sys.argv[1]
# VIDIOC_QUERYCAP = _IOR('V',0,struct v4l2_capability), 104 bytes on Linux.
VIDIOC_QUERYCAP=0x80685600
fd=os.open(node, os.O_RDONLY|os.O_NONBLOCK)
try:
    buf=bytearray(104)
    fcntl.ioctl(fd, VIDIOC_QUERYCAP, buf, True)
    caps=struct.unpack_from('I', buf, 84)[0]
    device_caps=struct.unpack_from('I', buf, 88)[0]
    effective=device_caps if (caps & 0x80000000) else caps
    sys.exit(0 if (effective & (0x00000001|0x00001000)) else 1)
finally:
    os.close(fd)
PY
}

list_capture_nodes() {
    local node real
    declare -A seen=()
    for node in /dev/v4l/by-id/*video-index0 /dev/video*; do
        [[ -e "$node" ]] || continue
        real="$(readlink -f "$node")"
        [[ -n "$real" && -z "${seen[$real]:-}" ]] || continue
        seen[$real]=1
        is_capture_node "$real" && printf '%s\n' "$real"
    done
}

find_preferred_device() {
    local node props vendor model
    local first="" elgato="" gc575=""
    while IFS= read -r node; do
        [[ -n "$first" ]] || first="$node"
        props="$(node_properties "$node")"
        vendor="$(property_value "$props" ID_VENDOR_ID)"
        model="$(property_value "$props" ID_MODEL_ID)"
        if [[ "$vendor" == "07ca" && "$model" == "0575" ]]; then
            gc575="$node"
        elif [[ "$vendor" == "0fd9" ]]; then
            [[ -n "$elgato" ]] || elgato="$node"
        fi
    done < <(list_capture_nodes)

    if [[ -n "$gc575" ]]; then
        printf '%s\n' "$gc575"
    elif [[ -n "$elgato" ]]; then
        printf '%s\n' "$elgato"
    elif [[ -n "$first" ]]; then
        printf '%s\n' "$first"
    else
        return 1
    fi
}

find_gc575_device() {
    local node props vendor model
    while IFS= read -r node; do
        props="$(node_properties "$node")"
        vendor="$(property_value "$props" ID_VENDOR_ID)"
        model="$(property_value "$props" ID_MODEL_ID)"
        if [[ "$vendor" == "07ca" && "$model" == "0575" ]]; then
            printf '%s\n' "$node"
            return 0
        fi
    done < <(list_capture_nodes)
    return 1
}

find_controller_bdf() {
    local vendor_file device_dir vendor product path base
    for vendor_file in /sys/bus/usb/devices/*/idVendor; do
        [[ -r "$vendor_file" ]] || continue
        device_dir="${vendor_file%/idVendor}"
        vendor="$(cat "$vendor_file" 2>/dev/null)"
        product="$(cat "$device_dir/idProduct" 2>/dev/null)"
        [[ "$vendor" == "07ca" && ( "$product" == "0575" || "$product" == "e575" ) ]] || continue
        path="$(readlink -f "$device_dir")"
        while [[ "$path" != "/" && -n "$path" ]]; do
            base="$(basename "$path")"
            if [[ -d "/sys/bus/pci/devices/$base" ]] && \
               [[ "$(cat "/sys/bus/pci/devices/$base/class" 2>/dev/null)" == "0x0c0330" ]]; then
                printf '%s\n' "$base"
                return 0
            fi
            path="$(dirname "$path")"
        done
    done
    return 1
}

remember_gc575_controller() {
    local bdf
    bdf="$(find_controller_bdf 2>/dev/null)" || return 0
    mkdir -p "$STATE_DIR"
    printf '%s\n' "$bdf" > "$STATE_FILE"
}

probe_device() {
    python3 - "$1" <<'PY' >/dev/null 2>&1
import subprocess, sys
try:
    result=subprocess.run(
        ['v4l2-ctl','-d',sys.argv[1],'--get-fmt-video'],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=4,
        check=False,
    )
    raise SystemExit(result.returncode)
except subprocess.TimeoutExpired:
    raise SystemExit(124)
PY
}

probe_gc575_format_engine() {
    local device="$1"
    local spec

    # Probe the physical transport that the requested session will actually
    # open. A hard-coded 1440p NV12 probe can report the engine as healthy and
    # then disturb a recovery restart that is about to open 1080p P010 or 4K
    # MJPEG. Smooth mode keeps the old probe, with a proven 1080p P010 liveness
    # fallback so the application can perform its bounded startup rollback.
    case "$SESSION_MODE" in
        hdr1080)
            spec="width=1920,height=1080,pixelformat=P010"
            ;;
        hdr1440)
            spec="width=2560,height=1440,pixelformat=NV12"
            ;;
        hdr4k)
            spec="width=3840,height=2160,pixelformat=MJPG"
            ;;
        *)
            if timeout 5s v4l2-ctl -d "$device" \
                --try-fmt-video=width=2560,height=1440,pixelformat=NV12 \
                >/dev/null 2>&1; then
                return 0
            fi
            spec="width=1920,height=1080,pixelformat=P010"
            ;;
    esac

    timeout 5s v4l2-ctl -d "$device" \
        --try-fmt-video="$spec" >/dev/null 2>&1
}

refresh_device_identity() {
    PROPS="$(node_properties "$DEVICE")"
    VENDOR_ID="$(property_value "$PROPS" ID_VENDOR_ID)"
    MODEL_ID="$(property_value "$PROPS" ID_MODEL_ID)"
    VENDOR_NAME="$(property_value "$PROPS" ID_VENDOR_FROM_DATABASE)"
    MODEL_NAME="$(property_value "$PROPS" ID_V4L_PRODUCT)"
    [[ -n "$MODEL_NAME" ]] || MODEL_NAME="$(property_value "$PROPS" ID_MODEL_FROM_DATABASE)"
    [[ -n "$MODEL_NAME" ]] || MODEL_NAME="$(property_value "$PROPS" ID_MODEL)"
    IS_GC575=0
    if [[ "$VENDOR_ID" == "07ca" && "$MODEL_ID" == "0575" ]]; then
        IS_GC575=1
        remember_gc575_controller
    fi
}

gc575_ready() {
    probe_device "$1" && probe_gc575_format_engine "$1"
}

wait_for_gc575_v4l2_ready() {
    local timeout_seconds="${1:-45}"
    local started now elapsed candidate announced=0 last_notice=-1

    started="$(date +%s)"
    while true; do
        udevadm settle --timeout=3 2>/dev/null || true
        candidate="$(find_gc575_device 2>/dev/null || true)"

        if [[ -n "$candidate" ]] && gc575_ready "$candidate"; then
            DEVICE="$candidate"
            refresh_device_identity
            echo "Linux Capture Studio: GC575 V4L2 capture node is ready at $DEVICE."
            return 0
        fi

        now="$(date +%s)"
        elapsed=$((now - started))
        if (( elapsed >= timeout_seconds )); then
            return 1
        fi

        if (( announced == 0 )); then
            echo "Linux Capture Studio: waiting for udev to recreate the GC575 V4L2 capture node..."
            announced=1
        elif (( elapsed >= 10 && elapsed / 10 != last_notice )); then
            last_notice=$((elapsed / 10))
            echo "Linux Capture Studio: still waiting for the GC575 video interface (${elapsed}s)..."
        fi
        sleep 1
    done
}

wait_for_gc575_node_only() {
    local timeout_seconds="${1:-45}"
    local started now elapsed candidate announced=0 last_notice=-1

    started="$(date +%s)"
    while true; do
        udevadm settle --timeout=3 2>/dev/null || true
        candidate="$(find_gc575_device 2>/dev/null || true)"
        if [[ -n "$candidate" ]]; then
            DEVICE="$candidate"
            refresh_device_identity
            echo "Linux Capture Studio: GC575 capture node returned at $DEVICE."
            return 0
        fi

        now="$(date +%s)"
        elapsed=$((now - started))
        if (( elapsed >= timeout_seconds )); then
            return 1
        fi
        if (( announced == 0 )); then
            echo "Linux Capture Studio: waiting for the GC575 capture node to return..."
            announced=1
        elif (( elapsed >= 10 && elapsed / 10 != last_notice )); then
            last_notice=$((elapsed / 10))
            echo "Linux Capture Studio: still waiting for the capture card (${elapsed}s)..."
        fi
        sleep 1
    done
}

recover_gc575_until_ready() {
    echo "Linux Capture Studio: recovering the GC575 capture-format engine..."
    "$ROOT_DIR/scripts/reset-gc575.sh" auto || true
    if wait_for_gc575_v4l2_ready 35; then
        return 0
    fi

    echo "Linux Capture Studio: the USB reset was not enough; escalating to the dedicated xHCI controller reset..."
    "$ROOT_DIR/scripts/reset-gc575.sh" --controller || true
    if wait_for_gc575_v4l2_ready 60; then
        return 0
    fi

    echo "ERROR: The GC575 USB device returned, but its V4L2 capture node did not become ready." >&2
    echo "Check: ls -l /dev/video* ; lsmod | grep uvcvideo" >&2
    return 1
}

usb_root_for_video() {
    local node="$1" path
    path="$(readlink -f "/sys/class/video4linux/$(basename "$node")/device" 2>/dev/null)"
    while [[ "$path" != "/" && -n "$path" ]]; do
        if [[ -r "$path/idVendor" && -r "$path/idProduct" ]]; then
            printf '%s\n' "$path"
            return 0
        fi
        path="$(dirname "$path")"
    done
    return 1
}

find_matching_audio_input() {
    local node="$1" usb_root pcm real base card device
    usb_root="$(usb_root_for_video "$node" 2>/dev/null)" || return 1
    for pcm in /sys/class/sound/pcmC*D*c; do
        [[ -e "$pcm" ]] || continue
        real="$(readlink -f "$pcm/device" 2>/dev/null)"
        [[ "$real" == "$usb_root"/* ]] || continue
        base="$(basename "$pcm")"
        if [[ "$base" =~ ^pcmC([0-9]+)D([0-9]+)c$ ]]; then
            card="${BASH_REMATCH[1]}"
            device="${BASH_REMATCH[2]}"
            printf 'plughw:%s,%s\n' "$card" "$device"
            return 0
        fi
    done
    return 1
}

find_matching_pulse_source() {
    command -v pactl >/dev/null 2>&1 || return 1
    python3 - "$VENDOR_ID" "$MODEL_ID" <<'PYJSON'
import json, subprocess, sys

def norm(value):
    if value is None:
        return ""
    value=str(value).lower().strip()
    if value.startswith("0x"):
        value=value[2:]
    return value.zfill(4)

vendor=norm(sys.argv[1])
model=norm(sys.argv[2])
try:
    raw=subprocess.check_output(
        ["pactl", "-f", "json", "list", "sources"],
        stderr=subprocess.DEVNULL,
        timeout=4,
    )
    data=json.loads(raw)
except Exception:
    raise SystemExit(1)

for source in data:
    props=source.get("properties") or {}
    sv=norm(props.get("device.vendor.id") or props.get("device.vendor_id"))
    sm=norm(props.get("device.product.id") or props.get("device.product_id"))
    description=(source.get("description") or "").lower()
    product=(props.get("device.product.name") or "").lower()
    match_ids=vendor and model and sv == vendor and sm == model
    match_name=(vendor == "07ca" and ("live gamer" in description or "live gamer" in product))
    if match_ids or match_name:
        name=source.get("name")
        if name:
            print(name)
            raise SystemExit(0)
raise SystemExit(1)
PYJSON
}

probe_pulse_input() {
    local source="$1"
    command -v gst-launch-1.0 >/dev/null 2>&1 || return 1
    timeout 4s gst-launch-1.0 -q \
        pulsesrc device="$source" num-buffers=48 \
        ! audioconvert ! audioresample ! fakesink sync=false \
        >/dev/null 2>&1
}

probe_audio_input() {
    local device="$1"
    command -v arecord >/dev/null 2>&1 || return 0
    python3 - "$device" <<'PY' >/dev/null 2>&1
import subprocess, sys
try:
    result = subprocess.run(
        [
            'arecord', '-q', '-D', sys.argv[1],
            '-f', 'S16_LE', '-r', '48000', '-c', '2',
            '-d', '1', '/dev/null'
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=3,
        check=False,
    )
    raise SystemExit(result.returncode)
except (subprocess.TimeoutExpired, FileNotFoundError):
    raise SystemExit(124)
PY
}

wait_for_audio_input() {
    local device="$1" attempt
    for attempt in 1 2 3; do
        probe_audio_input "$device" && return 0
        udevadm settle --timeout=2 2>/dev/null || true
        sleep 1
    done
    return 1
}

if [[ ! -x "$ROOT_DIR/build/src/linux-capture-studio" ]]; then
    echo "ERROR: Linux Capture Studio is not built."
    echo "Run: meson setup build && meson compile -C build"
    exit 1
fi

if [[ -z "$DEVICE" ]]; then
    DEVICE="$(find_preferred_device 2>/dev/null)"
fi

# If the previously used GC575 vanished completely, recover only that card.
# The USB ID can return several seconds before udev recreates /dev/video*.
if [[ -z "$DEVICE" && -f "$STATE_FILE" ]]; then
    echo "Linux Capture Studio: no capture node is present; attempting cached GC575 recovery..."
    "$ROOT_DIR/scripts/reset-gc575.sh" --controller || true
    if ! wait_for_gc575_v4l2_ready 60; then
        DEVICE=""
    fi
fi

if [[ -z "$DEVICE" ]]; then
    echo "ERROR: No V4L2 capture device was found."
    echo "Connect an AVerMedia, Elgato, or other UVC capture device and try again."
    exit 1
fi

refresh_device_identity

if [[ "$IS_GC575" -eq 1 ]]; then
    if [[ -n "${LINUX_CAPTURE_STUDIO_RECOVERY_RESTART:-}" ]]; then
        # The controller helper has already waited for the UVC capture node.
        # Do not run another v4l2-ctl TRY_FMT before GStreamer opens the exact
        # requested mode; that old probe was the source of repeated five-second
        # hangs during the visible 4K transition.
        if [[ ! -e "$DEVICE" ]]; then
            echo "ERROR: The GC575 capture node disappeared before restart." >&2
            exit 1
        fi
    elif ! gc575_ready "$DEVICE"; then
        echo "Linux Capture Studio: $DEVICE exists, but the GC575 format engine is not responding."
        if ! recover_gc575_until_ready; then
            echo "ERROR: GC575 is still rejecting capture formats after USB and controller recovery."
            exit 1
        fi
    fi
elif ! probe_device "$DEVICE"; then
    echo "Linux Capture Studio: $DEVICE exists but is not responding."
    echo "This non-GC575 device was not reset automatically. Reconnect its USB cable and retry."
    exit 1
fi

AUDIO_DEVICE="${LINUX_CAPTURE_STUDIO_AUDIO:-}"
if [[ -z "$AUDIO_DEVICE" ]]; then
    PULSE_SOURCE="$(find_matching_pulse_source 2>/dev/null || true)"
    if [[ -n "$PULSE_SOURCE" ]] && probe_pulse_input "$PULSE_SOURCE"; then
        AUDIO_DEVICE="pulse:$PULSE_SOURCE"
    else
        AUDIO_DEVICE="$(find_matching_audio_input "$DEVICE" 2>/dev/null)"
    fi
fi
[[ -n "$AUDIO_DEVICE" ]] || AUDIO_DEVICE="default"
AUDIO_CAPTURE_DISABLED=0
if [[ "$AUDIO_DEVICE" == pulse:* ]]; then
    if ! probe_pulse_input "${AUDIO_DEVICE#pulse:}"; then
        AUDIO_CAPTURE_DISABLED=1
        echo "Linux Capture Studio: PipeWire capture source ${AUDIO_DEVICE#pulse:} is unavailable; starting with silent audio fallback." >&2
    fi
elif [[ "$AUDIO_DEVICE" != "default" ]] && ! wait_for_audio_input "$AUDIO_DEVICE"; then
    AUDIO_CAPTURE_DISABLED=1
    echo "Linux Capture Studio: capture audio $AUDIO_DEVICE is not ready; starting video with silent audio fallback." >&2
fi

printf 'Using capture video: %s\n' "$DEVICE"
printf 'Detected device: %s%s%s\n' \
    "${VENDOR_NAME:-${VENDOR_ID:-Unknown vendor}}" \
    "${MODEL_NAME:+ — }" \
    "${MODEL_NAME:-}"
printf 'Capture audio input: %s\n' "$AUDIO_DEVICE"

if [[ "$IS_GC575" -eq 1 && "$NATIVE_P010" -eq 1 ]]; then
    echo "Linux Capture Studio: preparing native-resolution HDR60 session ($HDR60_OUTPUT)..."
    echo "Linux Capture Studio: the application will validate a source at the selected resolution; lower-resolution upscaling is disabled."
    sleep 2
fi

LINUX_CAPTURE_STUDIO_DEVICE="$DEVICE" \
LINUX_CAPTURE_STUDIO_AUDIO="$AUDIO_DEVICE" \
LINUX_CAPTURE_STUDIO_NATIVE_P010="$NATIVE_P010" \
LINUX_CAPTURE_STUDIO_NATIVE_HDR_MODE="$NATIVE_HDR_MODE" \
LINUX_CAPTURE_STUDIO_HDR60_OUTPUT="$HDR60_OUTPUT" \
LINUX_CAPTURE_STUDIO_SESSION_MODE="$SESSION_MODE" \
LINUX_CAPTURE_STUDIO_DISABLE_CAPTURE_AUDIO="$AUDIO_CAPTURE_DISABLED" \
LINUX_CAPTURE_STUDIO_VENDOR_ID="$VENDOR_ID" \
LINUX_CAPTURE_STUDIO_MODEL_ID="$MODEL_ID" \
"$ROOT_DIR/build/src/linux-capture-studio"
RESULT=$?

# Exit 75 is an intentional GC575 format-family transition.  The application
# writes the requested session before exiting; reset the capture-format engine
# once and restart directly in that mode instead of retrying the rejected mode
# inside the same UVC session.
if [[ "$RESULT" -eq 75 && "$IS_GC575" -eq 1 ]]; then
    PENDING_SESSION=""
    if [[ -r "$PENDING_SESSION_FILE" ]]; then
        PENDING_SESSION="$(head -n1 "$PENDING_SESSION_FILE" 2>/dev/null)"
    fi
    rm -f "$PENDING_SESSION_FILE"
    case "$PENDING_SESSION" in
        hdr1080|hdr1440|hdr4k|smooth) ;;
        *)
            echo "ERROR: GC575 transition requested without a valid target session." >&2
            exit 75
            ;;
    esac

    update_transition_status "Please wait — switching the capture card to native 4K60…"
    if [[ -x "$TRANSITION_WAIT_BIN" ]]; then
        "$TRANSITION_WAIT_BIN" "$TRANSITION_STATUS_FILE" >/dev/null 2>&1 &
        TRANSITION_WAIT_PID=$!
        # Give the progress window a moment to map before the controller and
        # main capture process disappear. The helper has a different process
        # name, so the privileged reset does not terminate it.
        sleep 0.25
    else
        TRANSITION_WAIT_PID=""
        echo "WARNING: transition progress window is unavailable; rebuild the complete project." >&2
    fi

    update_transition_status "Please wait — resetting the capture card…"
    echo "Linux Capture Studio: performing one controlled xHCI reset for the $PENDING_SESSION transition..."
    if ! "$ROOT_DIR/scripts/reset-gc575.sh" --controller; then
        update_transition_status "ERROR: The capture-card reset helper failed. Reinstall the updated helper and try again."
        echo "ERROR: The GC575 controller reset failed." >&2
        exit 1
    fi

    update_transition_status "Please wait — the capture card is reconnecting…"
    if ! wait_for_gc575_node_only 60; then
        update_transition_status "ERROR: The capture card did not return. Reconnect it and try again."
        echo "ERROR: The GC575 did not recover for the requested mode transition." >&2
        exit 1
    fi

    update_transition_status "Please wait — opening the native 4K60 preview and waiting for the first real frame…"
    echo "Linux Capture Studio: reopening directly in $PENDING_SESSION after recovery."
    LINUX_CAPTURE_STUDIO_RECOVERY_RESTART=1 \
    LINUX_CAPTURE_STUDIO_TRANSITION_STATUS_FILE="$TRANSITION_STATUS_FILE" \
    LINUX_CAPTURE_STUDIO_SESSION_MODE="$PENDING_SESSION" \
        exec "$0" "$DEVICE"
fi

if [[ "$RESULT" -eq 137 ]]; then
    echo "Linux Capture Studio was killed by SIGKILL (code 137)." >&2
    echo "The integrated launcher did not attempt a controller reset." >&2
elif [[ "$RESULT" -ne 0 ]]; then
    echo "Linux Capture Studio exited with code $RESULT. No controller reset was attempted." >&2
fi

if [[ -z "${LINUX_CAPTURE_STUDIO_RECOVERY_RESTART:-}" ]]; then
    rm -f "$TRANSITION_STATUS_FILE"
fi
exit "$RESULT"
