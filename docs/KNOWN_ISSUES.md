# Known Issues

- Native P010 transport can be sensitive to repeated low-level mode changes on the GC575. Use the staged Apply workflow and allow the brief release delay to finish.
- Processed P010 output cannot create color precision absent from an 8-bit NV12 source.
- Native P010 above 1080p60 depends on what the Linux UVC interface exposes. When unavailable, the application can retain native 1440p/4K resolution through NV12 or MJPEG transport, but converting that transport to P010 cannot create additional source bit depth.
- Streaming availability depends on installed H.264, AAC, FLV, and RTMP GStreamer elements.
- Elgato support is experimental until tested on physical hardware.
- A direct capture preview includes USB, application, and compositor latency and cannot exactly match a television's HDMI passthrough path.
