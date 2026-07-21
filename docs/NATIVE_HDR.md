# Native-resolution HDR60

Linux Capture Studio treats the selected HDR60 resolution as a physical source requirement.

- 1080p requires 1920×1080 at 60 FPS.
- 1440p requires 2560×1440 at 60 FPS.
- 4K requires 3840×2160 at 60 FPS.

The resolver prefers native P010. When a capture device exposes only NV12 or MJPEG at 1440p/4K60, the application can retain the selected native spatial resolution and convert to the P010 working format. That conversion cannot create source bit depth that was not delivered by the device, but it also does not perform spatial upscaling.

If no transport exists at the selected resolution and frame rate, the mode is rejected instead of falling back to a smaller source.
