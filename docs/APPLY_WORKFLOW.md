# Staged capture settings

Linux Capture Studio 0.6.30 does not rebuild the capture pipeline as soon as a selector changes.

1. Select the desired session, format, resolution, frame rate, and HDR state.
2. The toolbar displays `CHANGES PENDING`.
3. Press `Apply Changes`.
4. The preview becomes black for at least 0.5 seconds so the Apply action is visually confirmed.
5. For output-only HDR60 changes, the P010/GL capture transport stays alive behind the blackout to avoid the previous crash.
6. For a physical format, resolution, frame-rate, SDR/HDR, or source-mode change, the pipeline is released and rebuilt while the blackout remains visible.
7. The preview returns and the toolbar reports `Applied ✓`.

Recording and streaming are disabled while capture changes are pending or being applied. This prevents files or streams from starting with a different mode than the one shown in the selectors.

## GC575 native 4K safety

The GC575 can enumerate both genuine 1080p P010 and native 3840×2160/60 MJPEG but reject a direct in-process switch with `VIDIOC_TRY_FMT: EIO`. RC8 handles that single format-family transition through the integrated launcher:

1. Preserve the complete last-good capture session.
2. Stop the P010 stream.
3. Perform one controlled dedicated-xHCI reset.
4. Reopen directly in native 4K60.
5. Commit the session only after ten real frames arrive.
6. If negotiation still fails, return to genuine P010 1080p60 without retrying 4K.

Always start the application with `./scripts/run-linux-capture-studio.sh` so this transition can complete.
