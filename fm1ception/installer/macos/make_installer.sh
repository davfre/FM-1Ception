#!/bin/bash
# FM-1Ception macOS installer — Xfer Records
#
# Universal (arm64 + x86_64) Release build of the VST3, AU and Standalone -> Developer ID
# signed with the hardened runtime -> signed .pkg with one choice per format -> notarized
# and stapled.
#
# Usage:
#   installer/macos/make_installer.sh [--skip-build] [--no-notarize]
#
# The installer's name comes from installer/label.txt ("1.0.0b1"), which starts with the
# project's version in CMakeLists.txt.
#
# Environment overrides:
#   FM1C_APP_IDENTITY     "Developer ID Application: ..."
#   FM1C_PKG_IDENTITY     "Developer ID Installer: ..."
#   FM1C_NOTARY_PROFILE   notarytool keychain profile (default fm1ception-notary)
set -euo pipefail

SKIP_BUILD=0
NOTARIZE=1
for arg in "$@"; do
    case "$arg" in
        --skip-build)  SKIP_BUILD=1 ;;
        --no-notarize) NOTARIZE=0 ;;
        *) echo "Unknown option: $arg"; echo "Usage: $0 [--skip-build] [--no-notarize]"; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export PATH="$HOME/.cargo/bin:$PATH"

APP_IDENTITY="${FM1C_APP_IDENTITY:-Developer ID Application: Your Name (TEAMID)}"
PKG_IDENTITY="${FM1C_PKG_IDENTITY:-Developer ID Installer: Your Name (TEAMID)}"
NOTARY_PROFILE="${FM1C_NOTARY_PROFILE:-fm1ception-notary}"

PROD="FM-1Ception"
TARGET="FM1Ception"
ID="com.xferrecords.fm1ception"
VERSION="$(sed -nE 's/^project\(FM1Ception VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)"
LABEL="$(tr -d '[:space:]' < installer/label.txt)"
[ -n "$VERSION" ] || { echo "ERROR: could not read the version from CMakeLists.txt"; exit 1; }
case "$LABEL" in
    "$VERSION"*) ;;
    *) echo "ERROR: installer/label.txt ($LABEL) does not start with the project's version ($VERSION)"; exit 1 ;;
esac

BUILD_DIR="$ROOT/build-release"
ART="$BUILD_DIR/${TARGET}_artefacts/Release"
ENT="$BUILD_DIR/${TARGET}_artefacts/JuceLibraryCode"
RES_DIR="$ROOT/installer/macos"
OUT_DIR="$ROOT/Installers"
STAGING="$BUILD_DIR/installer_staging"
PKG="$OUT_DIR/Install_${PROD}_${LABEL}.pkg"

echo "========================================="
echo "Building $PROD $LABEL macOS installer"
echo "========================================="

# --- 1. Universal Release build -------------------------------------------------
if [ "$SKIP_BUILD" = 0 ]; then
    echo ""
    echo "Configuring universal Release build in $BUILD_DIR ..."
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DFM1C_BUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" --target ${TARGET}_VST3 ${TARGET}_AU ${TARGET}_Standalone --parallel
fi

VST3="$ART/VST3/${PROD}.vst3"
AU="$ART/AU/${PROD}.component"
APP="$ART/Standalone/${PROD}.app"
for b in "$VST3" "$AU" "$APP"; do
    [ -d "$b" ] || { echo "ERROR: $b not found. Build first (or drop --skip-build)."; exit 1; }
    ARCHS="$(lipo -archs "$b/Contents/MacOS/$PROD")"
    echo "Built $(basename "$b") ($ARCHS)"
    case "$ARCHS" in
        *arm64*x86_64*|*x86_64*arm64*) ;;
        *) echo "ERROR: $(basename "$b") is not universal ($ARCHS)"; exit 1 ;;
    esac
done

# --- 2. Stage + sign ------------------------------------------------------------
rm -rf "$STAGING"
mkdir -p "$STAGING/root_vst3" "$STAGING/root_au" "$STAGING/root_app" "$STAGING/resources" "$OUT_DIR"
cp -R "$VST3" "$STAGING/root_vst3/"
cp -R "$AU" "$STAGING/root_au/"
cp -R "$APP" "$STAGING/root_app/"
find "$STAGING" \( -name '._*' -o -name '.DS_Store' \) -delete
chmod -R 755 "$STAGING/root_vst3/${PROD}.vst3" "$STAGING/root_au/${PROD}.component" "$STAGING/root_app/${PROD}.app"
xattr -cr "$STAGING/root_vst3" "$STAGING/root_au" "$STAGING/root_app"

echo ""
echo "Codesigning with hardened runtime..."
sign() {   # <bundle> <entitlements>
    codesign --force --options runtime --timestamp --entitlements "$2" --sign "$APP_IDENTITY" "$1"
    codesign --verify --deep --strict --verbose=1 "$1"
}
sign "$STAGING/root_vst3/${PROD}.vst3"      "$ENT/${TARGET}_VST3.entitlements"
sign "$STAGING/root_au/${PROD}.component"   "$ENT/${TARGET}_AU.entitlements"
sign "$STAGING/root_app/${PROD}.app"        "$ENT/${TARGET}_Standalone.entitlements"
codesign -d --entitlements - "$STAGING/root_app/${PROD}.app" 2>/dev/null | grep -q allow-jit \
    || { echo "ERROR: the Standalone lacks the allow-jit entitlement"; exit 1; }

# --- 3. Component packages ------------------------------------------------------
echo ""
echo "Building component packages..."
build_component() {   # <root> <identifier> <install-dir> <out.pkg>
    local root="$1" ident="$2" dest="$3" out="$4"
    local plist="$STAGING/$(basename "$out").plist"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    # Stop PackageKit from "relocating" the install onto a stray copy of the bundle elsewhere on disk.
    plutil -replace 0.BundleIsRelocatable -bool false "$plist"
    pkgbuild --root "$root" --identifier "$ident" --version "$VERSION" \
        --component-plist "$plist" --install-location "$dest" \
        --scripts "$RES_DIR/scripts" \
        --sign "$PKG_IDENTITY" --timestamp "$out" >/dev/null
    echo "  $(basename "$out")  ->  $dest"
}
build_component "$STAGING/root_vst3" "${ID}.vst3" "/Library/Audio/Plug-Ins/VST3"       "$STAGING/${ID}.vst3.pkg"
build_component "$STAGING/root_au"   "${ID}.au"   "/Library/Audio/Plug-Ins/Components" "$STAGING/${ID}.au.pkg"
build_component "$STAGING/root_app"  "${ID}.app"  "/Applications"                      "$STAGING/${ID}.app.pkg"

# --- 4. Product archive ---------------------------------------------------------
echo ""
echo "Building installer package..."
cp "$ROOT/installer/License.txt" "$STAGING/resources/License.txt"
sed "s/@LABEL@/${LABEL}/g" "$RES_DIR/distribution.xml" > "$STAGING/distribution.xml"
rm -f "$PKG"
productbuild --distribution "$STAGING/distribution.xml" \
    --package-path "$STAGING" \
    --resources "$STAGING/resources" \
    --sign "$PKG_IDENTITY" \
    --timestamp \
    "$PKG"
echo "Installer package created: $PKG"

# --- 5. Notarize + staple -------------------------------------------------------
NOTARIZED=0
if [ "$NOTARIZE" = 1 ]; then
    echo ""
    echo "Submitting for notarization ($NOTARY_PROFILE)..."
    if xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait | tee "$STAGING/notary.log"; then
        if grep -q "status: Accepted" "$STAGING/notary.log"; then
            NOTARIZED=1
            for i in $(seq 1 15); do
                if xcrun stapler staple "$PKG"; then echo "Stapled."; break; fi
                if [ "$i" -lt 15 ]; then
                    echo "Ticket not available yet, retrying in 10 s ($i/15)..."
                    sleep 10
                else
                    echo "WARNING: stapling failed. Retry later with: xcrun stapler staple '$PKG'"
                fi
            done
        fi
    fi
    if [ "$NOTARIZED" = 0 ]; then
        echo "WARNING: notarization failed or was rejected; Gatekeeper will block the download."
        echo "  xcrun notarytool log <submission-id> --keychain-profile $NOTARY_PROFILE"
    fi
fi

rm -rf "$STAGING"

echo ""
echo "========================================="
echo "macOS installer complete"
echo "========================================="
echo "Output:     $PKG"
echo "Notarized:  $([ "$NOTARIZED" = 1 ] && echo yes || echo no)"
echo ""
