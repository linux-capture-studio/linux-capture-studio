# GC575 HDR implementation notes

## Signal identification

Linux Capture Studio queries `VIDIOC_G_FMT` and displays:

- `colorspace`
- `xfer_func`
- current pixel format

A BT.2020 colorspace combined with `V4L2_XFER_FUNC_SMPTE2084` is treated as HDR10/PQ in Auto mode. The Linux V4L2 transfer-function enumeration does not provide a dedicated HLG value, so HLG is a manual selection.

## GStreamer colorimetry

HDR10 recordings are tagged `colorimetry=bt2100-pq`.
HLG recordings are tagged `colorimetry=bt2100-hlg`.
SDR recordings are tagged `colorimetry=bt709`.

## Preview tone mapping

Linux Capture Studio 0.3.6 prefers the OpenGL path because the Fedora/Mesa AMD VA postprocessor may exist without exposing HDR tone mapping:

```text
P010 + BT.2100 colorimetry
  → glupload
  → glcolorconvert
  → glshader (PQ or HLG EOTF, BT.2020→BT.709, highlight compression)
  → glcolorconvert
  → gldownload
  → BGRx / BT.709
  → gtk4paintablesink
```

If the OpenGL elements are unavailable, Linux Capture Studio checks whether `vapostproc` exposes `hdr-tone-mapping` and uses that as a fallback. If neither path is usable, the switch turns itself off rather than constructing an invalid pipeline. The recording branch remains untouched by preview tone mapping. Native P010 is preserved when the device exposes it; native-resolution transport conversion is reported explicitly when required.

## Remaining HDR work

- Read static mastering-display and content-light metadata from the GC575 vendor-control path.
- Preserve that metadata in the Matroska track when it becomes available.
- Add an HDR-aware Wayland presentation path when GTK/GDK exposes stable HDR surface metadata.
- Add HEVC Main10 hardware encoding after the Fedora AMD encoder stack exposes a usable encoder.
