#!/bin/bash
# Build every FM-1Ception installer from the Mac — Xfer Records
#
# macOS is built natively; Windows is built inside the Parallels VM through `prlctl exec`,
# reading this checkout (and the Libraries/Juce beside it) over the Parallels shared "Home" folder.
# Everything lands in <repo>/Installers; per-platform logs go to <repo>/build-logs.
#
# Usage:
#   installer/build_all.sh [mac] [windows] [--no-notarize] [--skip-build]
#     no platform argument = both, built in parallel.
#     --skip-build repackages the existing Release builds (signing, notarization, installers only).
#
# Environment overrides:
#   FM1C_WIN_VM     Parallels VM name (auto-detected: "Windows*")
#   FM1C_WIN_REPO   repo path as seen from inside the VM
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGS="$ROOT/build-logs"
OUT="$ROOT/Installers"
mkdir -p "$LOGS" "$OUT"

DO_MAC=0; DO_WIN=0; MAC_ARGS=(); WIN_ARGS=""
for arg in "$@"; do
    case "$arg" in
        mac|macos)     DO_MAC=1 ;;
        win|windows)   DO_WIN=1 ;;
        --no-notarize) MAC_ARGS+=("--no-notarize") ;;
        --skip-build)  MAC_ARGS+=("--skip-build"); WIN_ARGS=" --skip-build" ;;
        *) echo "Unknown option: $arg"; echo "Usage: $0 [mac] [windows] [--no-notarize] [--skip-build]"; exit 1 ;;
    esac
done
if [ $((DO_MAC + DO_WIN)) -eq 0 ]; then DO_MAC=1; DO_WIN=1; fi

# The shared "Home" folder exposes $HOME as \\Mac\Home.
case "$ROOT" in
    "$HOME"/*) REL="${ROOT#"$HOME"/}" ;;
    *) REL="" ;;
esac
WIN_REPO="${FM1C_WIN_REPO:-}"
if [ -n "$REL" ] && [ -z "$WIN_REPO" ]; then WIN_REPO='\\Mac\Home\'"${REL//\//\\}"; fi

run_mac() {
    echo "[mac] building..."
    bash "$ROOT/installer/macos/make_installer.sh" "${MAC_ARGS[@]+"${MAC_ARGS[@]}"}" > "$LOGS/macos.log" 2>&1
}

run_windows() {
    local vm="${FM1C_WIN_VM:-$(prlctl list -a -o name 2>/dev/null | tail -n +2 | grep -i -m1 windows)}"
    [ -n "$vm" ] || { echo "[windows] ERROR: no Windows VM found (set FM1C_WIN_VM)"; return 1; }
    [ -n "$WIN_REPO" ] || { echo "[windows] ERROR: repo is outside \$HOME; set FM1C_WIN_REPO"; return 1; }
    if [ "$(prlctl list -a -o status,name 2>/dev/null | grep -F " $vm" | awk '{print $1}')" != "running" ]; then
        echo "Starting VM \"$vm\"..."
        prlctl start "$vm" || return 1
    fi
    # Parallels Tools need a moment after boot before exec works.
    local ready=0
    for _ in $(seq 1 30); do
        prlctl exec "$vm" --current-user cmd /c echo ok >/dev/null 2>&1 && { ready=1; break; }
        sleep 5
    done
    [ "$ready" = 1 ] || { echo "[windows] ERROR: VM \"$vm\" did not become ready"; return 1; }
    echo "[windows] building in \"$vm\" from $WIN_REPO ..."
    # --current-user: the logged-in user's session has Visual Studio, CMake, Rust and Inno Setup.
    prlctl exec "$vm" --current-user cmd /c "${WIN_REPO}\\installer\\windows\\build_installer.bat${WIN_ARGS}" > "$LOGS/windows.log" 2>&1
}

START=$(date +%s)
RESULT_mac=""; RESULT_windows=""
WIN_PID=""
if [ "$DO_WIN" = 1 ]; then run_windows & WIN_PID=$!; fi
if [ "$DO_MAC" = 1 ]; then run_mac; RESULT_mac=$?; fi
if [ -n "$WIN_PID" ]; then wait "$WIN_PID"; RESULT_windows=$?; fi

echo ""
echo "========================================="
echo "Installer summary  ($(( ($(date +%s) - START) / 60 )) min)"
echo "========================================="
FAILED=0
for p in mac windows; do
    eval "rc=\$RESULT_$p"
    [ -n "$rc" ] || continue
    log="$p.log"; [ "$p" = mac ] && log="macos.log"
    if [ "$rc" = 0 ]; then
        echo "  $p: OK        (log: build-logs/$log)"
    else
        echo "  $p: FAILED    (log: build-logs/$log)"; FAILED=1
    fi
done
if [ -n "$RESULT_mac" ] && grep -q "Notarized:  no" "$LOGS/macos.log" 2>/dev/null; then
    echo "  mac: package is signed but NOT notarized — see build-logs/macos.log"
fi
echo ""
ls -la "$OUT" | grep -E "\.(pkg|exe)$" || echo "  (no installers found in $OUT)"
echo ""
exit $FAILED
