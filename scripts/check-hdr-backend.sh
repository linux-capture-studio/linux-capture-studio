#!/usr/bin/env bash

DEVICE="${1:-${OPENCENTRAL_DEVICE:-/dev/video0}}"

printf '%s\n' '=== Linux Capture Studio HDR backend check ==='
printf 'Device: %s\n\n' "$DEVICE"

if command -v v4l2-ctl >/dev/null 2>&1 && [[ -e "$DEVICE" ]]; then
    echo '=== Current GC575 colorspace metadata ==='
    timeout 5s v4l2-ctl -d "$DEVICE" --get-fmt-video 2>&1 |
        grep -E 'Width/Height|Pixel Format|Colorspace|Transfer Function|YCbCr|Quantization' || true
    echo

    echo '=== Native P010 modes ==='
    timeout 8s v4l2-ctl -d "$DEVICE" --list-formats-ext 2>&1 |
        sed -n "/'P010'/,/^[[:space:]]*\[[0-9]\]/p" || true
    echo
fi

echo '=== HDR recording path ==='
if gst-inspect-1.0 avenc_ffv1 >/dev/null 2>&1; then
    echo 'PASS: FFV1 lossless encoder is present.'
else
    echo 'FAIL: avenc_ffv1 is missing.'
    echo 'Install it with: sudo dnf install gstreamer1-plugin-libav'
fi

echo
echo '=== OpenGL HDR-to-SDR preview path ==='
GL_OK=1
for element in glupload glcolorconvert glshader gldownload; do
    if gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'PASS: %s is present.\n' "$element"
    else
        printf 'FAIL: %s is missing.\n' "$element"
        GL_OK=0
    fi
done

if [[ "$GL_OK" -eq 1 ]]; then
    CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/linux-capture-studio"
    GL_MARKER="$CACHE_DIR/hdr-gl-ok"
    TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/linux-capture-studio-hdr-test.XXXXXX")
    SHADER_FILE="$TEST_DIR/pq-to-sdr.frag"

    cleanup_hdr_test() {
        rm -rf "$TEST_DIR"
    }
    trap cleanup_hdr_test EXIT

    mkdir -p "$CACHE_DIR"
    rm -f "$GL_MARKER"

    cat >"$SHADER_FILE" <<'SHADER_EOF'
#version 100
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
vec3 pq(vec3 n){const float m1=0.1593017578125;const float m2=78.84375;const float c1=0.8359375;const float c2=18.8515625;const float c3=18.6875;vec3 p=pow(max(n,vec3(0.0)),vec3(1.0/m2));return pow(max(p-vec3(c1),vec3(0.0))/max(vec3(c2)-vec3(c3)*p,vec3(0.000001)),vec3(1.0/m1));}
vec3 gamut(vec3 c){mat3 m=mat3(1.6605,-0.5876,-0.0728,-0.1246,1.1329,-0.0083,-0.0182,-0.1006,1.1187);return m*c;}
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
void main(){vec4 s=texture2D(tex,v_texcoord);vec3 l=gamut(pq(clamp(s.rgb,0.0,1.0)));vec3 o=pow(aces(max(l,vec3(0.0))*49.261),vec3(1.0/2.2));gl_FragColor=vec4(o,s.a);}
SHADER_EOF

    # gst-launch parses its own pipeline language after the shell has parsed
    # argv.  Keep literal double quotes around the complete multiline shader,
    # matching the documented glshader example.
    SHADER_ARG="fragment=\"$(cat "$SHADER_FILE")\""

    echo
    echo 'Testing PQ -> SDR OpenGL shader pipeline...'
    timeout --signal=KILL 20s gst-launch-1.0 -q \
        videotestsrc num-buffers=30 \
        ! 'video/x-raw,format=P010_10LE,colorimetry=bt2100-pq,width=640,height=360,framerate=30/1' \
        ! glupload ! glcolorconvert \
        ! 'video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D' \
        ! glshader "$SHADER_ARG" \
        ! 'video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D' \
        ! glcolorconvert \
        ! 'video/x-raw(memory:GLMemory),format=RGBA' \
        ! gldownload ! videoconvert \
        ! 'video/x-raw,format=BGRx,colorimetry=bt709' \
        ! fakesink sync=false
    GL_RESULT=$?

    if [[ "$GL_RESULT" -eq 0 ]]; then
        printf 'gstreamer=%s\nchecked=%s\n' \
            "$(gst-launch-1.0 --version | head -n1)" \
            "$(date --iso-8601=seconds)" >"$GL_MARKER"
        echo 'PASS: OpenGL HDR-to-SDR preview backend works.'
        printf 'Approval marker: %s\n' "$GL_MARKER"
    else
        echo "FAIL: OpenGL tone-map test exited with code $GL_RESULT."
        echo 'Leave HDR -> SDR preview disabled. HDR recording remains available.'
        GL_OK=0
    fi
fi

echo
echo '=== Optional VA-API path ==='
if gst-inspect-1.0 vapostproc >/dev/null 2>&1; then
    echo 'PASS: vapostproc is present.'
    if gst-inspect-1.0 vapostproc 2>/dev/null | grep -q 'hdr-tone-mapping'; then
        echo 'PASS: vapostproc exposes hdr-tone-mapping.'
    else
        echo 'INFO: this AMD VA-API driver does not expose hdr-tone-mapping.'
    fi
else
    echo 'INFO: vapostproc is unavailable.'
fi

echo
if [[ "$GL_OK" -eq 1 ]]; then
    echo 'PASS: automatic HDR preview correction is available in Linux Capture Studio.'
    exit 0
fi

echo 'HDR recording is available, but SDR preview tone mapping is unavailable.'
exit 1
