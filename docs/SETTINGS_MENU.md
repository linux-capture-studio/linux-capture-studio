# Settings menu and audio routing

Linux Capture Studio 0.3.9 moves recording, image, and sound controls into one top-right GTK4 popover.

## Sound page

- `Capture input` is the ALSA source used for the recording and live monitor. The GC575 default is `hw:L21,0`.
- `Monitor output` is populated from `pactl list sinks`.
- `System default (PipeWire)` leaves device routing to the desktop session.
- Selecting a listed sink changes the `pulsesink device` property on Linux Capture Studio's monitor branch only.
- `Refresh` scans again after USB, HDMI, Bluetooth, or headset devices are connected.

The recording audio is always sourced from the capture-input field; the monitor-output choice changes only where the live sound is heard.

## Movable settings window

Settings opens as an independent, resizable GTK window. It is not anchored to the toolbar and can be moved normally.

## Built-in HDR calibration

The Image & HDR tab includes display peak, paper white, black floor, and saturation controls. Applying calibration updates the live preview shader and saves values per capture device. Recording is not modified.
