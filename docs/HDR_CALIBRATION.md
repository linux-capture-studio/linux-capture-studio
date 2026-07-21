# HDR calibration

Linux Capture Studio 0.6.30 RC12 includes a guided three-step calibration workflow modeled after console HDR setup screens. It calibrates the software preview tone mapper and never modifies captured or recorded HDR samples.

## Steps

1. **Maximum luminance** — raise the value until the inner bright symbol is barely visible, then move back one step.
2. **Reference white** — choose a comfortable white level for menus, subtitles, and ordinary bright objects.
3. **Black level** — raise the value until the dark symbol appears, then lower it until it is barely visible.

The values are stored per capture device. While moving the control, the active shader is updated when possible. Finishing the wizard always performs a clean preview-renderer refresh behind a 500 ms blackout so the saved shader values are definitely active.

## Scope

Calibration affects only the preview path. Recording remains untouched. A native 4K60 MJPEG source is native resolution but remains an 8-bit source; converting it to the P010 working format does not create genuine 10-bit detail.

## Source-aware preview mapping (0.6.29)

The calibration values are applied differently according to the physical capture transport. Genuine P010 is interpreted as PQ/HLG. MJPEG, NV12, and YUYV are treated as 8-bit display-referred sources and are never decoded as PQ; doing so would crush shadows. This affects preview only.
