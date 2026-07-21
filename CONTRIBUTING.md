# Contributing

Linux Capture Studio is an independent native Linux capture application. Contributions should preserve the stable GC575 path and avoid proprietary vendor code or binaries.

## Before opening a pull request

1. Build with Meson and Ninja.
2. Run `./scripts/release-check.sh`.
3. Test ordinary NV12 preview before testing HDR, recording, or streaming.
4. Describe the capture card, video node, selected mode, Linux distribution, GTK version, and GStreamer version.
5. Attach diagnostics produced by `./scripts/collect-crash-diagnostics.sh` when reporting crashes.

Do not include stream keys, account credentials, copyrighted vendor installers, firmware, extracted proprietary libraries, or recordings containing private material.
