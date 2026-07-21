# Changelog

## 0.6.30

- Fixes the false rollback success shown when a rejected GC575 mode entered provisional GStreamer `PLAYING` but never delivered a real frame.
- Rollback now remains in progress until the restored pipeline produces at least ten frames over 0.3 seconds; asynchronous `TRY_FMT` failures can no longer be reported as a successful restore.
- After three failed rollback attempts, performs one controlled reset into the proven genuine P010 1080p60 safety mode instead of reopening the rejected mode indefinitely.
- Strengthens Apply validation to require at least 30 frames and 0.8 seconds of stable video before a physical mode becomes the new last-good mode.
- Keeps a permanent GL shader stage in native HDR sessions, allowing HDR preview correction, HDR signal-mode changes, and calibration to update live without closing or reopening the V4L2 capture device.
- Disabling HDR preview correction now swaps to a pass-through shader rather than rebuilding the GC575 pipeline.
- Retains the persistent native-4K transition window, source-aware HDR brightness mapping, native-resolution capture, and bounded startup recovery.

## 0.6.29 RC11

- Keeps a dedicated **Please wait** window visible across the GC575 xHCI reset and application restart.
- Closes the transition window only after the native 4K pipeline delivers its first real frame.
- Removes repeated five-second 1440p NV12 `TRY_FMT` probes from the privileged reset helper.
- Stops the reset helper from killing GUI processes, preserving the transition window.
- Detects an outdated installed reset helper and requests a one-time helper refresh instead of running the old loop.
- Gives native 4K MJPEG a 25-second first-frame grace period and a 12-second sustained-stall threshold.
- Keeps native 3840×2160 at 60 FPS, source-aware HDR preview correction, console calibration, and safe rollback.
- Stops the one-second statistics/UI timer before GTK destroys its labels, preventing repeated invalid-widget callbacks during shutdown.

## 0.6.28 RC10

- Fixed the startup crash/reopen storm when the GC575 asynchronously rejects a saved mode such as NV12 2560×1440.
- Recovery attempts are now counted until a real video frame arrives; a provisional GStreamer PLAYING result no longer resets the counter.
- Stops after three failed attempts instead of rebuilding the same rejected pipeline forever.
- When no mode has produced a frame, performs one controlled GC575 reset and reopens in the proven genuine P010 1080p60 safety mode.
- Marks the recovery restart so a failed safety mode cannot trigger another launcher-reset loop.
- Preserves the RC9 source-aware HDR brightness correction and native-resolution 4K behavior.

## 0.6.27 RC9

- Fixed the severely dark native-4K HDR preview shown when the GC575 uses its 8-bit MJPEG transport.
- Added source-aware HDR processing: only genuine P010 is decoded with the PQ/HLG transfer function.
- MJPEG, NV12, and YUYV HDR sessions now use a display-referred compensation curve instead of an incorrect ST-2084 decode.
- Replaced the shadow-crushing ACES preview curve with a console-style paper-white/highlight roll-off for genuine P010.
- HDR calibration now changes the correct active-source preview shader, including the 4K MJPEG session.
- Recording/output branches remain untouched.

## 0.6.26 RC8

- Fixes the GC575 1080p P010 → native 4K60 MJPEG transition by routing it through one controlled format-engine reset and launcher restart.
- Defers Apply success until the first real video frame arrives; asynchronous V4L2 negotiation errors can no longer be reported as success.
- Restores the complete last-good session, including HDR session mode, instead of retrying a rejected 4K mode.
- Stops failed-mode retry loops and safely returns to the proven preview.

- Removed the blocking `VIDIOC_TRY_FMT` readiness probe from the GTK main thread after the KDE coredump showed Apply trapped inside that ioctl.
- Removed the stale `GtkMenuButton` cast from fullscreen handling.
- Replaced the technical HDR sliders with a three-step console-style calibration wizard: maximum luminance, reference white, and black level.
- HDR calibration now updates the live shader and then performs a clean 500 ms preview-renderer refresh to guarantee the values are active.
- HDR preview correction now really rebuilds the preview renderer when switched on or off.
- Settings is an independent, freely movable and resizable application window on KDE/Wayland.
- Preserved true native-resolution 4K60 capture; lower-resolution scaling remains disabled.

## 0.6.24 RC6

- Replaced the attached settings popover with a freely movable, resizable top-level GTK settings window.
- Added built-in HDR preview calibration for peak luminance, paper white, black floor, and saturation.
- Added live GLSL shader calibration updates without changing the recording branch.
- Persisted HDR calibration per capture device.
- Changed HDR60 1440p and 4K sessions to require a native source at the selected resolution.
- Removed the hidden 1080p/1440p upscaling fallback for 4K HDR60.
- Kept native P010 preferred while allowing native-resolution transport conversion when required by Linux UVC exposure.

## 0.6.23 RC5

- Added a mandatory 0.5-second black confirmation screen whenever `Apply Changes` commits output-only settings.
- Kept the active P010/GL transport alive behind the blackout, preventing the previous HDR Apply crash.
- Physical transport changes still stop and rebuild the capture pipeline safely, with the blackout held until preview returns.
- Apply success and failure paths now always remove the blackout and restore the controls.

## 0.6.22 RC4

- Fixed HDR preview GLSL delivery: shader source is now assigned directly after pipeline creation, preserving real line breaks.
- Shader compilation failures now fall back safely instead of triggering capture-device recovery loops.
- YouTube and Twitch remain disabled and labeled Coming Soon; recording remains enabled.
- Flatpak packaging remains network-disabled while streaming is unavailable.


## 0.6.20-rc.2

- Added automatic HDR preview correction for genuine P010/PQ and HLG sources.
- Corrected the BT.2020-to-BT.709 preview gamut matrix.
- Kept HDR recording untouched while tone-mapping only the desktop preview.
- Added a GNOME 50 Flatpak manifest and local bundle builder.
- Kept recording enabled.
- Disabled YouTube and Twitch controls and marked them Coming Soon.
- Removed network permission from the Flatpak sandbox.

## 0.6.19-rc.1

- Added per-user installation, desktop integration, AppStream metadata, CI, and release documents.
- Kept the proven 0.6.18 Apply workflow and capture engine.

## 0.6.18

- Added staged capture changes and an explicit Apply Changes workflow.
- Preserved the same application window while rebuilding the active capture pipeline.
