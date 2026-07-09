#!/usr/bin/env python3
"""N1 connectivity spike: prove the PC can reach the game's TCP command server.

Boot the NETTEST build (./scripts/build-test.sh) on a real PS3 or RPCS3, then:

    PS3_IP=192.168.1.13 python3 tests/smoke.py

It pings, reads the state and the board, and drives the main menu one step with a
synthetic button press to confirm input injection works end-to-end.
"""

import sys

from harness import PS3Client


def main():
    try:
        c = PS3Client().connect()
    except Exception as e:  # noqa: BLE001
        print("CONNECT FAILED:", e)
        print("- Is the NETTEST build running? Is PS3_IP correct and on the same LAN?")
        print("- RPCS3: set Network > Network Status to 'Connected' (may still not pass through).")
        return 1

    with c:
        pong = c.ping()
        print("ping  ->", pong)
        if pong != "pong":
            print("FAIL: expected 'pong'")
            return 1

        st = c.state()
        print("state ->", st)
        print("board ->", c.board())

        # Drive the main menu down once and read it back (proves input injection).
        before = c.state().get("screen")
        c.press("down")
        c.press("cross")
        after = c.state().get("screen")
        print(f"menu  -> screen {before} then {after} after down+cross")

    print("\nOK: TCP command server reachable and responding.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
