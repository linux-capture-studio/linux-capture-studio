# Capture-device support

Linux Capture Studio 0.5.0 uses V4L2 capability enumeration rather than a fixed brand table.

## Stable development device

- AVerMedia Live Gamer 4K 2.1 (GC575)

## Experimental UVC devices

- Elgato Game Capture HD60 X
- Elgato Game Capture 4K X
- Elgato Cam Link 4K
- Other capture devices exposing V4L2 Video Capture and Streaming capabilities

## Rules

- Only formats returned by `VIDIOC_ENUM_FMT` are considered native.
- Only sizes returned by `VIDIOC_ENUM_FRAMESIZES` are exposed.
- Only rates returned by `VIDIOC_ENUM_FRAMEINTERVALS` appear in the FPS menu.
- HDR requires an actual P010 mode from the device.
- GC575 PCIe/xHCI recovery is never applied to another vendor.

Elgato support is currently community-tested rather than vendor-supported on Linux. Use `collect-capture-device-profile.sh` when reporting a device.
