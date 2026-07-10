# RayChess e2e test harness

Drive the running game over TCP from Python and assert that the rules and UI behave
correctly — automated verification instead of manually checking each rule on the
console.

## How it works

The game, built with the **NETTEST** flag, runs a line-based TCP command server on
**port 9010** (`source/nettest/`). The Python client (`harness.py`) connects and
sends one command per line; the game replies with one line. Commands either **drive**
the game (`newgame`, `move`, `press`, `promote`) or **query** it (`state`, `board`,
`legal`, `at`, `turn`, `history`, `points`). All game state is touched only on the
main thread, so this can't race the renderer.

## 1. Build the test binary

```bash
DOCKER_DEFAULT_PLATFORM=linux/amd64 ./scripts/build-test.sh        # -> src.fake.self
```

`build-test.sh` cleans first on purpose: the Makefile's incremental build doesn't
track the `NETTEST` flag, so a stale `Game.o` compiled without it would leave the
server dormant. The normal `./scripts/build.sh` is unchanged and opens no port.

## 2. Run it

- **RPCS3:** `/Applications/RPCS3.app/Contents/MacOS/rpcs3 src.fake.self`
  (Configuration → Network → Network Status = **Connected**.) RPCS3 exposes the guest
  listener on the host, so `PS3_IP=127.0.0.1` works.
- **Real PS3 (HEN/CFW):** install `src.pkg` or `ps3load src.fake.self`; use the
  console's LAN IP.

Sanity-check the connection (game at the menu):

```bash
lsof -nP -iTCP -sTCP:LISTEN | grep 9010     # should show rpcs3/… :9010 (LISTEN)
PS3_IP=127.0.0.1 python3 tests/smoke.py
```

## 3. Run the suite

```bash
cd tests
python3 -m venv .venv          # isolated env (Homebrew Python is externally managed)
source .venv/bin/activate
pip install pytest
PS3_IP=127.0.0.1 pytest -v     # or: pytest --ps3-ip 192.168.1.13
```

> Use a venv (or `python3 -m pytest`), not a bare system `pytest`: on macOS the
> system `pip` is externally managed (PEP 668) and a bare `pytest` may even resolve
> to Python 2, which chokes on the suite's f-strings. `deactivate` leaves the venv.

Tests share one connection and each starts from a fresh game (`classic`/`ffa`/`teams`
fixtures), so they're order-independent. If the game isn't reachable the whole suite
is skipped with a clear message.

## Command reference

| Command | Reply |
|---|---|
| `ping` | `pong` |
| `state` | `screen=… mode=… turn=… state=… round=… check=0/1 gameover=0/1` (4PC adds `current`/`promo`) |
| `board` | classic: FEN placement; 4PC: 14 rows of 14 two-char cells joined by `/` |
| `turn` | `white`/`black` or `red`/`blue`/`yellow`/`green` |
| `at <sq>` | `white peon` / `empty` / `off` (`<sq>` = `e2` or `i,j`) |
| `legal <sq>` | space-separated destinations, or `none` |
| `history` | SAN move list (classic) |
| `points` | `red=… blue=… yellow=… green=…` (4PC) |
| `newgame classic\|ffa\|teams` | `ok` |
| `move <from> <to>` | `ok` / `err …` (`e2 e4`, `e2e4`, or 4PC `12,5 11,5`) |
| `promote q\|r\|b\|n` | `ok` / `err …` |
| `press <btn>` | `ok` (`cross circle triangle square up down left right start select l1 r1`) |
| `setup <fen> [w\|b]` | `ok` — classic: load a FEN placement (+ active colour) to test a position |
| `setup4 <turn> <c><t>@i,j …` | `ok` — 4PC: place pieces (`c`∈r/b/y/g, `t`∈p/r/n/b/q/k) for scenarios |
| `setclock <w> <b>` | `ok` — classic: force the Fischer clock active with these seconds (flag-fall tests) |
| `result` | classic terminal state, or the 4PC game-over message (`Equipo A … gana!` / `Empate …`) |

## Notes

- The `[nettest]` diagnostics print to the PS3 TTY → RPCS3's log
  (`~/Library/Application Support/rpcs3/RPCS3.log`), not to stdout.
- This is opt-in tooling: the shipped PKG (plain `build.sh`) contains none of it.
