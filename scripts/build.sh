#!/usr/bin/env bash
#
# Build the PS3 raylib Test .self using the Dockerized PSL1GHT toolchain (the
# canonical flow). No local toolchain install required.
#
# Usage:
#   ./scripts/build.sh            # build -> src.self (named after the /src mount)
#   ./scripts/build.sh clean      # clean the build
#   ./scripts/build.sh pkg        # build an installable PKG
#
# Env:
#   PS3_TOOLCHAIN_IMAGE   Docker image to use
#                         (default: ghcr.io/02900/ps3-toolchain-raylib:latest)
#   PS3_NO_PULL=1         Skip the docker pull freshness check (use cached image)
#
# Note: on Apple Silicon add `--platform linux/amd64` to the docker run below.
#
set -euo pipefail

IMAGE="${PS3_TOOLCHAIN_IMAGE:-ghcr.io/02900/ps3-toolchain-raylib:latest}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Pull the toolchain image first so image-side updates actually land — `docker run`
# reuses the locally cached image otherwise. This is a fast digest check when the
# image is already current. Skip with PS3_NO_PULL=1 (offline / pinned).
if [ "${PS3_NO_PULL:-0}" != "1" ]; then
  echo ">> Pulling $IMAGE (set PS3_NO_PULL=1 to skip)"
  docker pull "$IMAGE" || echo ">> pull failed (offline?), using cached image" >&2
fi

TARGET=""
case "${1:-build}" in
  build)      TARGET="" ;;
  clean)      TARGET="clean" ;;
  pkg)        TARGET="pkg" ;;
  *)          echo "Unknown target '${1}'. Use: build | clean | pkg" >&2; exit 1 ;;
esac

echo ">> Building with $IMAGE (make $TARGET)"

# The PPU gcc 7.2 toolchain occasionally segfaults (cc1 / collect2) when run under
# emulation (x86_64 image on Apple Silicon). These crashes are transient and
# unrelated to the code, so retry a few times before giving up.
attempts=3
for try in $(seq 1 "$attempts"); do
  if docker run --rm -v "$REPO_ROOT":/src -w /src "$IMAGE" make $TARGET; then
    exit 0
  fi
  echo ">> build attempt $try/$attempts failed" >&2
  [ "$try" -lt "$attempts" ] && echo ">> retrying (toolchain may have segfaulted under emulation)..." >&2
done
echo ">> build failed after $attempts attempts" >&2
exit 1
