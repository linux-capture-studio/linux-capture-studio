#!/usr/bin/env bash

SELF="$(readlink -f "$0")"
if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    exec sudo "$SELF"
fi
TARGET_USER="${SUDO_USER:-}"
rm -f /usr/libexec/linux-capture-studio-reset
if [[ -n "$TARGET_USER" && "$TARGET_USER" != "root" ]]; then
    rm -f "/etc/sudoers.d/linux-capture-studio-reset-$TARGET_USER"
fi
rm -rf /var/lib/linux-capture-studio
echo "Linux Capture Studio passwordless reset helper removed."
