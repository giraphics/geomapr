#!/usr/bin/env bash
# ============================================================
#  geomapr - macOS build (MapViewerUnrealEditor, Mac)
#
#  Close the Unreal Editor before running.
#  Override the engine location with:  UE_ROOT=/path/to/UE_5.8 ./build.sh
# ============================================================
set -euo pipefail

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$HERE/MapViewerUnreal.uproject"
BATCH="$UE_ROOT/Engine/Build/BatchFiles/Mac"

if [ ! -x "$BATCH/Build.sh" ]; then
    echo "ERROR: Unreal Engine not found at \"$UE_ROOT\""
    echo "Set UE_ROOT to your UE 5.8 install folder and re-run."
    exit 1
fi

echo "Generating Xcode project files ..."
"$BATCH/GenerateProjectFiles.sh" -project="$PROJECT" -game

echo "Building MapViewerUnrealEditor Mac Development ..."
"$BATCH/Build.sh" MapViewerUnrealEditor Mac Development -project="$PROJECT" -waitmutex

echo "Build SUCCEEDED - open with:  open \"$PROJECT\""
