#!/usr/bin/env bash

SELF="$(readlink -f "$0")"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HELPER_SOURCE="$ROOT_DIR/scripts/linux-capture-studio-reset-helper"
HELPER_TARGET="/usr/libexec/linux-capture-studio-reset"
STATE_DIR="/var/lib/linux-capture-studio"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    echo "Linux Capture Studio: one-time administrator authorization is required to install the restricted reset helper."
    echo "After this setup, automatic GC575 recovery will not ask for a password."
    exec sudo "$SELF"
fi

TARGET_USER="${SUDO_USER:-}"
if [[ -z "$TARGET_USER" || "$TARGET_USER" == "root" ]]; then
    echo "ERROR: Run this installer as your normal user, not from a root login." >&2
    exit 1
fi
if [[ ! "$TARGET_USER" =~ ^[a-z_][a-z0-9_-]*[$]?$ ]]; then
    echo "ERROR: Unsupported user name: $TARGET_USER" >&2
    exit 1
fi
if [[ ! -x "$HELPER_SOURCE" ]]; then
    echo "ERROR: Missing helper: $HELPER_SOURCE" >&2
    exit 1
fi

install -d -m 0755 -o root -g root /usr/libexec
install -m 0755 -o root -g root "$HELPER_SOURCE" "$HELPER_TARGET"
install -d -m 0755 -o root -g root "$STATE_DIR"

# Cache the controller only from the live AVerMedia USB topology. Never trust a
# user-writable BDF file for a passwordless privileged operation.
find_live_controller() {
    local vendor_file device_dir vendor product path base
    for vendor_file in /sys/bus/usb/devices/*/idVendor; do
        [[ -r "$vendor_file" ]] || continue
        device_dir="${vendor_file%/idVendor}"
        vendor="$(cat "$vendor_file" 2>/dev/null)"
        product="$(cat "$device_dir/idProduct" 2>/dev/null)"
        [[ "$vendor" == "07ca" && ( "$product" == "0575" || "$product" == "e575" ) ]] || continue
        path="$(readlink -f "$device_dir")"
        while [[ "$path" != "/" && -n "$path" ]]; do
            base="$(basename "$path")"
            if [[ -d "/sys/bus/pci/devices/$base" ]] && \
               [[ "$(cat "/sys/bus/pci/devices/$base/class" 2>/dev/null)" == "0x0c0330" ]]; then
                printf '%s\n' "$base"
                return 0
            fi
            path="$(dirname "$path")"
        done
    done
    return 1
}

if BDF="$(find_live_controller 2>/dev/null)"; then
    printf '%s\n' "$BDF" > "$STATE_DIR/gc575-xhci-bdf"
    chown root:root "$STATE_DIR/gc575-xhci-bdf"
    chmod 0600 "$STATE_DIR/gc575-xhci-bdf"
    echo "Linux Capture Studio: cached dedicated controller $BDF."
else
    echo "WARNING: The capture controller could not be cached because the AVerMedia device is not currently visible."
    echo "Run this installer again once 07ca:0575 or 07ca:e575 is present."
fi

SUDOERS_TARGET="/etc/sudoers.d/linux-capture-studio-reset-$TARGET_USER"
SUDOERS_TMP="$(mktemp)"
trap 'rm -f "$SUDOERS_TMP"' EXIT
cat > "$SUDOERS_TMP" <<SUDOERS
# Linux Capture Studio: passwordless access only to the root-owned, argument-restricted GC575 reset helper.
$TARGET_USER ALL=(root) NOPASSWD: $HELPER_TARGET auto, $HELPER_TARGET --soft, $HELPER_TARGET --controller, $HELPER_TARGET --full
SUDOERS
chmod 0440 "$SUDOERS_TMP"
if ! visudo -cf "$SUDOERS_TMP"; then
    echo "ERROR: Refusing to install an invalid sudoers rule." >&2
    exit 1
fi
install -m 0440 -o root -g root "$SUDOERS_TMP" "$SUDOERS_TARGET"

if sudo -u "$TARGET_USER" sudo -n "$HELPER_TARGET" --soft >/dev/null 2>&1; then
    echo "Linux Capture Studio: passwordless reset helper installed and verified."
else
    # A soft reset can legitimately fail if the function is absent. Verify the
    # authorization rule itself without triggering an interactive prompt.
    if sudo -u "$TARGET_USER" sudo -n -l "$HELPER_TARGET" >/dev/null 2>&1; then
        echo "Linux Capture Studio: passwordless reset authorization installed."
    else
        echo "WARNING: The helper was installed, but noninteractive authorization could not be verified." >&2
    fi
fi

echo "Future automatic recovery will not request your password."
