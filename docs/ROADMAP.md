# Linux Capture Studio roadmap after RECentral audit

## V0.2 — included

- GTK4/Wayland live preview.
- GC575 profile selection for 1080p60, 1440p60, 1440p144, 4K60 and 1080p240.
- Matroska recording using GC575 MJPEG passthrough and synchronized 48 kHz stereo PCM.
- Crash-resistant recording container and EOS finalization.
- Elapsed time and file-size telemetry.
- Native brightness, contrast, saturation and hue controls through V4L2.
- Read-only extraction of the two `07ca:e575` HID report descriptors.

## V0.3

- Audio endpoint discovery and per-source meters.
- Separate HDMI/game and microphone/aux tracks where the two GC575 USB audio interfaces expose them.
- Hardware H.264/HEVC encoding after Mesa VA-API encoder support is restored on the host.
- MP4 remux/export.
- dropped-frame and timestamp telemetry.

## V0.4

- Screenshot capture and stream-health telemetry.
- RTMP/SRT streaming.
- Scene compositor, image/text/webcam sources and profile persistence.

## Control track

- Decode both E575 HID report descriptors.
- Capture only user-authorized input/output observations from hardware owned by the user.
- Implement RGB first because the official module identifies it as HID-based.
- Treat firmware writing, HDCP changes and EDID writes as separate high-risk operations; never guess command payloads.
