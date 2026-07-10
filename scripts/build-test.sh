#!/usr/bin/env bash
#
# Build the RayChess .self / .pkg WITH the e2e test-harness TCP server compiled in
# (-DNETTEST + source/nettest). This opens a TCP command server on port 9010 so the
# Python pytest harness (tests/) can drive the game and assert rules over the network.
#
# Do NOT ship this build — use plain ./scripts/build.sh for releases (no port opened).
#
# Usage:
#   ./scripts/build-test.sh          # build -> src.self with the test server
#   ./scripts/build-test.sh pkg      # installable PKG with the test server
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export NETTEST=1

# The Makefile's incremental build does NOT track the NETTEST flag, so switching
# between a normal build and a test build would otherwise leave Game.o compiled
# WITHOUT -DNETTEST (nettest::Start() never called → no server). Force a clean
# rebuild so every object is compiled with the flag.
echo ">> cleaning first (NETTEST toggles the whole build)"
"$REPO_ROOT/scripts/build.sh" clean
exec "$REPO_ROOT/scripts/build.sh" "$@"
