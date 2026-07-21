# Integrated capture sessions

Linux Capture Studio uses one launcher and one application window for every capture session.

- Smooth SDR defaults to native NV12 2560×1440 at 60 FPS when exposed by the device.
- HDR60 Native 1080p opens a physical 1920×1080/60 source.
- HDR60 Native 1440p opens a physical 2560×1440/60 source.
- HDR60 Native 4K opens a physical 3840×2160/60 source.

Native P010 is preferred. If Linux exposes only NV12 or MJPEG at a selected higher resolution, Linux Capture Studio keeps that full physical resolution and converts pixel format only. It does not silently use a lower-resolution source and upscale it.

Session changes are staged and committed with **Apply Changes**. Native source-resolution changes rebuild the capture transport behind the visible Apply blackout. Settings that resolve to the same physical source keep the transport alive.
