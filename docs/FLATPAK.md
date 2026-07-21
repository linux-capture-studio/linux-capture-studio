# Flatpak packaging

Linux Capture Studio uses `org.gnome.Platform//50` and `org.gnome.Sdk//50`.

Build a local bundle on Fedora:

```bash
sudo dnf install flatpak flatpak-builder
./scripts/build-flatpak.sh
```

The script creates:

```text
Linux-Capture-Studio-0.6.30.flatpak
```

Install and run it:

```bash
flatpak install --user --reinstall ./Linux-Capture-Studio-0.6.30.flatpak
flatpak run io.github.linuxcapturestudio.LinuxCaptureStudio
```

Sandbox permissions are limited to Wayland/fallback X11, PulseAudio/PipeWire, GPU access, capture-device nodes, and the Videos folder. Network access is intentionally absent while YouTube and Twitch are marked Coming Soon.
