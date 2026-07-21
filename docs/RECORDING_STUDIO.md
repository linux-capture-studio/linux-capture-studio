# Recording Studio

Linux Capture Studio keeps the stable recording architecture used by the 0.5.x builds.

## Lossless path

Raw NV12, YUYV, and native/processed P010 are encoded with FFV1 inside Matroska. MJPEG can be preserved without unnecessary video re-encoding. Audio is stored as 48 kHz stereo PCM in Matroska.

## Compressed path

High Quality, Balanced, and Small File use H.264 when `x264enc` or `openh264enc` is available. The application checks the encoder before building the pipeline. When a compressed pipeline cannot start, it retries once with FFV1/MKV and updates the selected codec/container.

## Containers

Matroska is the compatibility-first container. MP4 is enabled only for H.264 with the required MP4 and AAC elements. The effective extension is generated after the final codec/container decision.

## Recording library

The Library page scans the selected output directory for MKV and MP4 recordings and provides playback, folder access, refresh, storage totals, and move-to-Trash controls.
