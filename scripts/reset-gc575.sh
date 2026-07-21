#!/usr/bin/env bash

MODE="${1:-auto}"
HELPER="/usr/libexec/linux-capture-studio-reset"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_HELPER="$ROOT_DIR/scripts/linux-capture-studio-reset-helper"

case "$MODE" in
    auto|--soft|--controller|--full) ;;
    *)
        echo "Usage: $0 [auto|--soft|--controller|--full]" >&2
        exit 2
        ;;
esac

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    if [[ -x "$HELPER" ]]; then
        exec "$HELPER" "$MODE"
    fi
    exec "$ROOT_DIR/scripts/linux-capture-studio-reset-helper" "$MODE"
fi

if [[ ! -x "$HELPER" ]]; then
    echo "Linux Capture Studio: the passwordless recovery helper is not installed."
    echo "Run this one-time setup while the card is healthy:"
    echo "  $ROOT_DIR/scripts/install-passwordless-reset.sh"
    exit 126
fi

if [[ -r "$SOURCE_HELPER" ]] && ! cmp -s "$SOURCE_HELPER" "$HELPER"; then
    echo "Linux Capture Studio: the installed GC575 reset helper is outdated." >&2
    echo "Install the RC12 helper once before switching to native 4K:" >&2
    echo "  $ROOT_DIR/scripts/install-passwordless-reset.sh" >&2
    exit 126
fi

# -n guarantees that the launcher never opens an interactive sudo prompt.
sudo -n "$HELPER" "$MODE"
rc=$?
if [[ "$rc" -ne 0 ]]; then
    if [[ "$rc" -eq 1 || "$rc" -eq 126 ]]; then
        echo "Linux Capture Studio: passwordless reset authorization is missing or outdated."
        echo "Run the one-time setup again:"
        echo "  $ROOT_DIR/scripts/install-passwordless-reset.sh"
    fi
    exit "$rc"
fi
