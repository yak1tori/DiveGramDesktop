#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# XWayland (xcb) is preferred: only it lets KWin apply the real KDE blur
# behind the transparent window (the iOS liquid-glass effect). Native
# Wayland is still translucent, but without compositor blur. Override with
# DG_COMPOSITOR=wayland|xcb|auto.
PLATFORM="${DG_COMPOSITOR:-auto}"
if [ "$PLATFORM" = "auto" ]; then
    if [ -n "$DISPLAY" ] && compgen -G "/tmp/.X11-unix/X*" >/dev/null; then
        PLATFORM=xcb
        WAYLAND_DISPLAY=""
    elif [ -n "$WAYLAND_DISPLAY" ]; then
        PLATFORM=wayland
    elif compgen -G "$XDG_RUNTIME_DIR/wayland-*" >/dev/null; then
        PLATFORM=wayland
        WAYLAND_DISPLAY="$(basename "$(compgen -G "$XDG_RUNTIME_DIR/wayland-*" | head -1)")"
    else
        PLATFORM=xcb
    fi
fi

# Drop orphaned containers left behind by aborted runs (timeout, kill).
# They keep the single-instance lock on the build tree and block the start.
docker ps -q --filter "ancestor=divegram_env" | xargs -r docker rm -f

# Make sure no stale native DiveGram processes are alive from this checkout.
if [ -x "$ROOT/out/Release/DiveGram" ]; then
    pgrep -f "$ROOT/out/Release/DiveGram" | xargs -r kill 2>/dev/null || true
fi

ARGS=(
    run --rm --network host
    -u "$(id -u)"
    -e "PLATFORM=$PLATFORM"
    -e "QT_QPA_PLATFORM=$PLATFORM"
    -e "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
    -e "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
    -e "DISPLAY=${DISPLAY:-:0}"
    -e "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
    -v "$XDG_RUNTIME_DIR:/run/user/$(id -u)"
    -v "$ROOT:/usr/src/tdesktop"
)

if [ -d /tmp/.X11-unix ]; then
    ARGS+=("-v" "/tmp/.X11-unix:/tmp/.X11-unix")
fi

# KWin (Plasma Wayland) starts XWayland with a per-session authority file
# in XDG_RUNTIME_DIR; plain X sessions use ~/.Xauthority.
if [ "$PLATFORM" = "xcb" ]; then
    XAUTH_FILE="$(ls "$XDG_RUNTIME_DIR"/xauth_* 2>/dev/null | head -1)"
    if [ -z "$XAUTH_FILE" ] && [ -f "$HOME/.Xauthority" ]; then
        XAUTH_FILE="$HOME/.Xauthority"
    fi
    if [ -n "$XAUTH_FILE" ]; then
        ARGS+=("-v" "$XAUTH_FILE:/home/user/.Xauthority" "-e" "XAUTHORITY=/home/user/.Xauthority")
    fi
fi

exec docker "${ARGS[@]}" divegram_env \
    bash -c "cd /usr/src/tdesktop/out/Release && exec ./DiveGram"