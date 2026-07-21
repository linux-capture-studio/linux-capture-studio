# Premium UI design notes

Linux Capture Studio 0.3.9 separates the interface into four visual layers:

1. **Studio header** — brand, readiness/recording state, settings, and fullscreen actions.
2. **Capture strip** — format, resolution, frame rate, HDR, and the primary recording action.
3. **Video stage** — a dark rounded preview surface with connection and active-mode badges.
4. **Status footer** — detailed pipeline messages and keyboard hints.

The settings popover uses card-style pages to keep secondary controls out of the live capture surface. Fullscreen removes all application chrome and overlays, leaving only the video.

The redesign intentionally does not alter any GStreamer pipeline descriptions or GC575 recovery behavior.
