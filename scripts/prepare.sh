#!/usr/bin/env bash
# Prepares a buildable Android project in third_party/winlator:
#   1. fetches the pinned Winlator source (last public source = Winlator 7.1)
#   2. fetches the OpenXR SDK the native code links against
#   3. extracts imagefs.txz (the Wine/Box64 system image) from the official
#      Winlator 7.1 APK, which is not stored in the Winlator source repo
#   4. copies this repo's overlay/ on top
#
# Usage: scripts/prepare.sh            (needs git, unzip, and gh or curl)
#        WINLATOR_APK=/path/to/Winlator_7.1.apk scripts/prepare.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=../winlator.lock
source "$ROOT/winlator.lock"

DEST="$ROOT/third_party/winlator"
# Never let checkout auto-download LFS files; imagefs is fetched explicitly below.
export GIT_LFS_SKIP_SMUDGE=1
DL="$ROOT/third_party/downloads"
mkdir -p "$DL"

# --- 1. Winlator source (sparse: only app/, skips ~350 MB of prebuilt add-ons)
if [ ! -d "$DEST/.git" ] || [ "$(git -C "$DEST" rev-parse HEAD 2>/dev/null)" != "$WINLATOR_COMMIT" ]; then
  echo ">> Fetching Winlator source @ $WINLATOR_COMMIT"
  rm -rf "$DEST"
  git init -q "$DEST"
  git -C "$DEST" remote add origin "$WINLATOR_REPO"
  git -C "$DEST" config core.sparseCheckout true
  printf '/app/\n/LICENSE\n' > "$DEST/.git/info/sparse-checkout"
  git -C "$DEST" fetch -q --depth 1 --filter=blob:none origin "$WINLATOR_COMMIT"
  git -C "$DEST" checkout -q FETCH_HEAD
else
  echo ">> Resetting Winlator source to pristine state"
  git -C "$DEST" reset -q --hard "$WINLATOR_COMMIT"
  git -C "$DEST" clean -qfdx -e app/src/main/cpp/OpenXR-SDK
fi

# --- 2. OpenXR SDK (the source tree ships an empty folder for it)
XR="$DEST/app/src/main/cpp/OpenXR-SDK"
if [ ! -f "$XR/CMakeLists.txt" ]; then
  echo ">> Fetching OpenXR-SDK $OPENXR_TAG"
  rm -rf "$XR"
  git clone -q --depth 1 -b "$OPENXR_TAG" https://github.com/KhronosGroup/OpenXR-SDK "$XR"
fi

# --- 3. imagefs.txz: Git LFS object of the pinned commit, else the official APK
IMAGEFS="$DEST/app/src/main/assets/imagefs.txz"
is_real_imagefs() { [ -s "$IMAGEFS" ] && ! head -c 200 "$IMAGEFS" | grep -q 'git-lfs.github.com/spec'; }

if ! is_real_imagefs && command -v git-lfs >/dev/null 2>&1; then
  echo ">> Trying Git LFS for imagefs.txz"
  git -C "$DEST" lfs install --local >/dev/null 2>&1 || true
  GIT_LFS_SKIP_SMUDGE=0 git -C "$DEST" lfs pull --include app/src/main/assets/imagefs.txz || echo "   LFS download failed, falling back to the APK"
fi

if ! is_real_imagefs; then
  rm -f "$IMAGEFS"
  APK="${WINLATOR_APK:-}"
  if [ -z "$APK" ]; then
    APK="$(ls "$DL"/*.apk 2>/dev/null | head -n1 || true)"
  fi
  if [ -z "$APK" ]; then
    echo ">> Downloading Winlator $WINLATOR_RELEASE_TAG APK"
    if command -v gh >/dev/null 2>&1 && [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ]; then
      gh release download "$WINLATOR_RELEASE_TAG" -R "${WINLATOR_REPO#https://github.com/}" -p '*.apk' -D "$DL" --clobber
    else
      curl -fL --retry 3 -o "$DL/$WINLATOR_APK_NAME" \
        "$WINLATOR_REPO/releases/download/$WINLATOR_RELEASE_TAG/$WINLATOR_APK_NAME"
    fi
    APK="$(ls "$DL"/*.apk | head -n1)"
  fi
  echo ">> Extracting imagefs.txz from $(basename "$APK")"
  ENTRY="$(unzip -Z1 "$APK" | grep -E '(^|/)imagefs[^/]*\.txz$' | head -n1 || true)"
  if [ -z "$ENTRY" ]; then
    echo "!! No imagefs*.txz found inside $APK. Contents:" >&2
    unzip -Z1 "$APK" | grep -E '^assets/' | head -50 >&2
    exit 1
  fi
  unzip -p "$APK" "$ENTRY" > "$IMAGEFS"
  echo "   $(du -h "$IMAGEFS" | cut -f1) imagefs.txz"
fi

# --- 4. Our overlay
echo ">> Applying overlay"
cp -R "$ROOT/overlay/." "$DEST/"

echo ">> Ready: build with  (cd third_party/winlator && gradle assembleDebug)"
