#!/usr/bin/env bash

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$HOME/Downloads/opencentral-e575-hid-contract-$STAMP"
ARCHIVE="$HOME/Downloads/opencentral-e575-hid-contract-$STAMP.tar.gz"
mkdir -p "$OUT"

{
    date --iso-8601=seconds
    uname -a
    lsusb -d 07ca:e575
    lsusb -v -d 07ca:e575
} > "$OUT/device.txt" 2>&1

{
    echo "This collector is read-only. It does not send HID feature reports or USB control transfers."
    echo
    for NODE in /dev/hidraw*; do
        [[ -e "$NODE" ]] || continue
        PROPS="$(udevadm info --query=property --name="$NODE" 2>/dev/null)"
        if ! grep -q '^ID_VENDOR_ID=07ca$' <<<"$PROPS" || \
           ! grep -q '^ID_MODEL_ID=e575$' <<<"$PROPS"; then
            continue
        fi

        echo "===== $NODE ====="
        printf '%s\n' "$PROPS"
        SYS_PATH="$(udevadm info --query=path --name="$NODE" 2>/dev/null)"
        echo "SYS_PATH=$SYS_PATH"

        HID_PARENT=""
        CUR="/sys$SYS_PATH"
        while [[ "$CUR" != "/sys" && "$CUR" != "/" ]]; do
            if [[ -r "$CUR/report_descriptor" ]]; then
                HID_PARENT="$CUR"
                break
            fi
            CUR="$(dirname "$CUR")"
        done

        if [[ -n "$HID_PARENT" ]]; then
            echo "REPORT_DESCRIPTOR=$HID_PARENT/report_descriptor"
            if command -v xxd >/dev/null 2>&1; then
                xxd -g1 "$HID_PARENT/report_descriptor"
            else
                od -An -tx1 -v "$HID_PARENT/report_descriptor"
            fi
        else
            echo "Report descriptor not found in sysfs ancestry"
        fi
        echo
    done
} > "$OUT/hidraw-and-report-descriptors.txt" 2>&1

{
    echo "=== USB topology ==="
    lsusb -t
    echo
    echo "=== hid-generic kernel messages ==="
    journalctl -k -b --no-pager 2>/dev/null | grep -Ei '07ca|e575|avermedia|hidraw|hid-generic'
} > "$OUT/kernel-and-topology.txt" 2>&1

tar -czf "$ARCHIVE" -C "$(dirname "$OUT")" "$(basename "$OUT")"
rm -rf "$OUT"

ls -lh "$ARCHIVE"
sha256sum "$ARCHIVE"
echo "Upload this archive for report-descriptor decoding and safe protocol planning."
