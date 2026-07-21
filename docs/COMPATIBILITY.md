# Compatibility

## Verified hardware

- AVerMedia Live Gamer 4K 2.1 (GC575)
  - NV12 2560×1440 at 60 FPS preview
  - Integrated native-resolution HDR60 workflow that prefers P010 and rejects lower-resolution scaling
  - PipeWire capture audio and independent monitoring
  - Lossless recording baseline

## Experimental hardware

- Elgato and generic UVC/V4L2 capture devices are discovered through their advertised capabilities.
- Actual modes depend on the device's Linux UVC descriptors and installed GStreamer elements.
- GC575-specific controller recovery is never applied to non-GC575 hardware.

Hardware not listed as verified should be treated as experimental until a device profile and real recording test are available.
