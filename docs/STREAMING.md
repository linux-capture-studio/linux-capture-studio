# Live Streaming

Linux Capture Studio 0.6.4 adds a standard RTMP/RTMPS output pipeline.

## Pipeline

- Native V4L2 capture remains visible in the local GTK preview.
- The outgoing video branch converts to I420 and H.264.
- The outgoing audio branch converts the selected ALSA input to stereo AAC.
- `flvmux` creates the live FLV stream.
- `rtmp2sink` is preferred; `rtmpsink` is used as a fallback.
- Capture rates above 60 FPS are reduced to 60 FPS on the outgoing branch only.

## Services

- **YouTube RTMPS:** paste the Stream URL shown by YouTube Live Control Room and then paste the stream key.
- **Twitch:** the default server field can use `rtmp://live.twitch.tv/app`; paste the private stream key separately.
- **Custom:** enter any valid `rtmp://` or `rtmps://` server URL and key.

## Security

The stream key uses `GtkPasswordEntry`, is hidden by default, is not saved in `profiles.ini`, and the full streaming pipeline is not printed because it contains the endpoint.

## Current limits

Streaming is SDR-only in this alpha. Recording and streaming are mutually exclusive to keep the capture device stable and avoid duplicate high-bandwidth encoders.
