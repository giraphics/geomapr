#!/usr/bin/env bash
# Deletes generated Unreal folders: Binaries, Intermediate, DerivedDataCache, Saved
set -euo pipefail
cd "$(dirname "$0")"
for d in Binaries Intermediate DerivedDataCache Saved; do
    [ -d "$d" ] && { echo "Removing $d ..."; rm -rf "$d"; }
done
echo "Clean complete. Run ./build.sh to rebuild."
