# Quality and smoothness

The GC575 safe transport uses native NV12 2560x1440 at 60 FPS for the smoothest uncompressed capture path. Linux Capture Studio keeps the preview on this native stream and performs P010 conversion only in recording/output branches.

NV12 is 8-bit 4:2:0. Converting it to P010 is mathematically lossless relative to those NV12 samples, but it cannot reconstruct precision that the device did not deliver. True native P010 remains limited to the modes exposed by the V4L2 device, and repeated physical format flips are disabled on the GC575 because they can wedge its UVC firmware.

For highest motion quality use NV12 2560x1440 at 60 FPS with Lossless FFV1. For true native 10-bit, use a capture mode/device that exposes stable native P010 at the required frame rate. 4K60 MJPEG is compressed by the capture device before Linux receives it.
