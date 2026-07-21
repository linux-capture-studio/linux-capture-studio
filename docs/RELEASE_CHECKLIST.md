# Release Checklist

- Run `./scripts/release-check.sh`.
- Build from a clean directory.
- Start NV12 1440p60 and confirm live sound.
- Stage a resolution or HDR change, then press Apply Changes.
- Confirm the preview pauses, applies, and returns in the same window.
- Record at least 20 seconds using the lossless preset and play it back.
- Confirm recording and streaming remain disabled while changes are pending.
- Open and close every settings page.
- Verify no stream key appears in logs or profile files.
- Create the source archive with timestamps normalized into the past.
