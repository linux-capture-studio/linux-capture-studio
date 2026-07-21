# VRR support

Linux Capture Studio treats Variable Refresh Rate honestly:

- VRR passthrough is negotiated between the HDMI source, capture card, and passthrough display.
- Standard V4L2 does not define a universal control that enables VRR on every capture card.
- The application keeps V4L2/GStreamer timestamps rather than inserting `videorate` into the capture path.
- The VRR monitor measures buffer presentation-timestamp intervals and reports either stable or variable frame timing.

A variable frame-timing report is evidence of changing captured cadence, not a guarantee that every HDMI VRR flag is exposed to Linux.
