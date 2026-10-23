#!/bin/sh
# Wrap the binary into PatientZero.app with an Info.plist. If raylib was
# linked dynamically, the dylib is copied into Frameworks and the load path
# rewritten so the bundle runs on machines without Homebrew raylib.
# Usage: sh scripts/bundle_macos.sh [binary]
set -e
BIN="${1:-patient_zero}"
APP="PatientZero.app"
VERSION="1.0.0"

if [ ! -f "$BIN" ]; then
    echo "error: binary '$BIN' not found - run 'make release' first" >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$BIN" "$APP/Contents/MacOS/PatientZero"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Patient Zero</string>
    <key>CFBundleDisplayName</key><string>Patient Zero</string>
    <key>CFBundleIdentifier</key><string>com.patientzero.game</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleExecutable</key><string>PatientZero</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><false/>
    <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
</dict>
</plist>
PLIST

# Bundle a dynamic raylib if the binary references one
if command -v otool >/dev/null 2>&1; then
    DYLIB=$(otool -L "$APP/Contents/MacOS/PatientZero" | awk '/libraylib/ {print $1; exit}')
    if [ -n "$DYLIB" ]; then
        REAL="$DYLIB"
        case "$DYLIB" in
            @rpath/*) for p in /opt/homebrew/lib /usr/local/lib; do
                          [ -f "$p/$(basename "$DYLIB")" ] && REAL="$p/$(basename "$DYLIB")"; done ;;
        esac
        if [ -f "$REAL" ]; then
            cp "$REAL" "$APP/Contents/Frameworks/"
            install_name_tool -change "$DYLIB" "@executable_path/../Frameworks/$(basename "$REAL")" "$APP/Contents/MacOS/PatientZero"
            echo "bundled $(basename "$REAL")"
        fi
    fi
fi

# ad-hoc sign so Gatekeeper lets it run locally
if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
fi
echo "created $APP"
