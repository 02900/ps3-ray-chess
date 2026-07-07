#!/usr/bin/env bash
#
# Send the PS3 raylib Test .self to a PS3 running the ps3load network receiver,
# using the Dockerized toolchain (so no local ps3dev install is required). The
# PS3 must be in its homebrew loader / ps3load listening state (PS3LoadX, TCP 4299).
#
# Usage:
#   PS3_IP=192.168.1.13 ./scripts/deploy.sh                  # sends src.self
#   ./scripts/deploy.sh 192.168.1.13                         # sends src.self
#   ./scripts/deploy.sh 192.168.1.13 some_other.self         # sends a specific .self
#
# Env:
#   PS3_IP                PS3 IP address (or pass as $1)
#   PS3_TOOLCHAIN_IMAGE   Docker image to use
#                         (default: ghcr.io/02900/ps3-toolchain-raylib:latest)
#
# Note: uses `--network host` so the container can reach the PS3 on your LAN.
#       (On Apple Silicon add `--platform linux/amd64` to the docker run below.)
#
set -euo pipefail

IMAGE="${PS3_TOOLCHAIN_IMAGE:-ghcr.io/02900/ps3-toolchain-raylib:latest}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PS3_IP="${PS3_IP:-${1:-}}"
SELF="${2:-src.self}"

if [ -z "$PS3_IP" ]; then
  echo "Usage: PS3_IP=<ip> $0   (or: $0 <ip> [self])" >&2
  exit 1
fi
if [ ! -f "$REPO_ROOT/$SELF" ]; then
  echo "Error: $SELF not found in $REPO_ROOT (build it first with scripts/build.sh)" >&2
  exit 1
fi

echo ">> Sending $SELF to PS3 at $PS3_IP via $IMAGE"
docker run --rm --network host \
  -e PS3LOAD="tcp:$PS3_IP" \
  -v "$REPO_ROOT":/src -w /src \
  "$IMAGE" \
  ps3load "$SELF"
