#!/bin/sh
set -eu
PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

# Source the Leaf platform env (paths, runtime dirs, appearance snapshot) when present.
PLATFORM="${PLATFORM:-mlp1}"
for root in "${SDCARD_PATH:-/mnt/sdcard}" /mnt/sdcard /media/sdcard1; do
  env_sh="$root/.system/leaf/platforms/$PLATFORM/launcher/env.sh"
  if [ -f "$env_sh" ]; then . "$env_sh"; break; fi
done

BIN="$PAK_DIR/bin/joes-calibrage"
# Logs belong in the durable user-data tree, never the release-managed pak dir.
LOG_DIR="${LOGS_PATH:-${USERDATA_PATH:-${SDCARD_PATH:-/mnt/sdcard}/.userdata/$PLATFORM}/logs}"
mkdir -p "$LOG_DIR" 2>/dev/null || LOG_DIR=/tmp

cd "$PAK_DIR"
exec "$BIN" 2>"$LOG_DIR/joes-calibrage.log"
