# RECentral 4 v4.7.114.1 static architecture findings

The official installer was inspected statically on Linux. No Windows executable, DLL, service, driver or MSI custom action was executed.

## Confirmed application architecture

- `RECentral 4.exe` is a 64-bit .NET/WPF application.
- The application embeds CefSharp/Chromium for web-backed login and service integrations.
- `RECentralService.exe` is a native 64-bit service/COM control process.
- The media engine is a large native DirectShow graph made from `.ax` filters and graph-controller DLLs.
- Recording/preview graph components include `CommonPreviewGraph.dll`, `CommonRecordGraph.dll`, `GraphManager.dll`, `AudioCaptureGraph.dll`, `UVCDeviceGraph.dll` and `PipCaptureGraph.dll`.
- RECentral ships its own FFmpeg generations, Media Foundation wrappers, MJPEG decoder, MP4/TS muxers, network sender, replay-buffer filters and GPU encoder adapters.
- GPU paths include AMD VCE, NVIDIA NVENC and Intel Quick Sync modules.

## Confirmed GC575 split

The installer recognizes `VID_07CA&PID_0575`.

- Video/audio media path: generic UVC/UAC device modules.
- Device API: `UVCDevice.dll` and `UVCDeviceControl.dll`.
- Preview/record graph: `UVCDeviceGraph.dll`.
- RGB path: `AVerLEDControlSDK.dll`, which imports Windows HID and SetupAPI.
- Realtek/device-control path: `AVerRtkControlSdk.dll` and `RTK_IO_x64.dll`.
- Firmware path: `AVerFirmwareUpdate.dll`.

Strings in the native device modules identify controls for serial number, firmware, HDR metadata, HDR NV12/P010 modes, HDR-to-SDR, EDID, HDCP state and USB link/device information.

## Linux mapping

| RECentral component | Native Linux replacement |
|---|---|
| WPF interface | GTK4/libadwaita |
| DirectShow graph | GStreamer graph |
| Windows UVC capture | V4L2 `uvcvideo` |
| Windows audio capture | ALSA/PipeWire |
| Media Foundation/FFmpeg filters | GStreamer codecs/muxers |
| Windows service/COM | D-Bus user service |
| HID/SetupAPI RGB SDK | hidraw/hidapi clean-room backend |
| Realtek control SDK | libusb/UVC extension-unit clean-room backend |
| Registry profiles | JSON profiles under XDG config |

The original binary cannot become a native ELF application without AVerMedia source code. Native Linux support therefore requires reimplementing these boundaries while preserving observable behavior, not loading or redistributing the Windows modules.
