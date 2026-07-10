# RayChess PS3

A PlayStation 3 port of **[ray-chess](https://github.com/GustavoHenriqueMuller/ray-chess)** — an
open-source chess game made in **[raylib](https://www.raylib.com/)** (C++) by Gustavo Henrique
Müller — running on PSL1GHT + RSXGL via [nbe1233/raylib-ps3](https://github.com/nbe1233/raylib-ps3).
It builds against the **raylib toolchain variant**
[`ghcr.io/02900/ps3-toolchain-raylib`](https://github.com/02900/ps3-toolchain).

Full chess rules — castling, en passant, promotion, check / checkmate / stalemate — come straight
from ray-chess's engine, reused essentially verbatim.

> ## Status: M1 — pipeline (playable milestones in progress)
> ray-chess is already a raylib game, so the port is a clean *"rind swap"*: the chess engine
> (`Board`, `pieces/*`, `Renderer`, most of `Game`) is reused as-is, and only the platform I/O layer
> is rewritten for the PS3. **M1** brought up the pipeline (board + all 32 pieces at 1280×720, assets
> embedded in the `.self`); **M2** makes it **playable with the controller** — a cursor over the
> squares (D-pad/stick), **Cross** to select/move, **Circle** to cancel, and a gamepad promotion
> menu — driving the engine's full rules (castling, en passant, promotion, check / checkmate /
> stalemate); **M3** adds the original click/cancel sound effects (via MikMod) and polish. The port
> is feature-complete versus ray-chess.

## Porting approach

ray-chess is a desktop raylib project; only the I/O rind is rewritten, the engine is reused as-is:

| ray-chess (desktop) | PS3 port |
|---------------------|----------|
| mouse: click a square (`GetMousePosition`) | gamepad cursor (D-pad/stick, Cross=confirm, Circle=cancel) |
| `std::filesystem` scan of `assets/textures` + `assets/sounds` | PNG/WAV embedded via `bin2o`, loaded from memory |
| `LoadImage` from a file path | `LoadImageFromMemory(".png", …)` on the embedded bytes |
| raylib `LoadSound`/`PlaySound` (no audio backend on RSXGL) | the real `click`/`clickCancel` WAVs via **MikMod** (`source/audio.c`) |
| 640×672 desktop window | 1280×720 framebuffer; the 640×672 canvas centered via a `Camera2D` offset |
| STL chess engine (board / pieces / moves / rules / rendering) | **kept as-is** — libstdc++ links via the C++ driver |

Two small toolchain adaptations were needed: `<filesystem>` is unavailable in the PS3 toolchain's
libstdc++ (removed with the runtime asset scan), and its newlib-backed libstdc++ omits
`std::to_string` — a one-function shim lives in [`source/compat.h`](source/compat.h).

## Building

Use the **raylib** toolchain image:

```bash
docker run --rm -v "$PWD":/src -w /src ghcr.io/02900/ps3-toolchain-raylib make      # -> ps3-ray-chess.self
docker run --rm -v "$PWD":/src -w /src ghcr.io/02900/ps3-toolchain-raylib make pkg  # -> ps3-ray-chess.pkg (XMB)
```

Or the helper wrappers (they default to the raylib image and auto-retry transient emulation
segfaults):

```bash
./scripts/build.sh            # build
./scripts/build.sh pkg        # installable PKG
PS3_IP=192.168.1.13 ./scripts/deploy.sh   # ps3load to a console running PS3LoadX
```

> **Platform notes** — **Apple Silicon:** add `--platform linux/amd64` to every `docker run`
> (or `export DOCKER_DEFAULT_PLATFORM=linux/amd64`; the helper scripts rely on that).
> **Windows:** run from a **WSL2** shell. **Linux:** prefix with `sudo` if needed.

Via the Docker helper the outputs are named after the `/src` mount: `src.elf` / `src.self` /
`src.pkg`. In CI (which checks out into a dir named after the repo) they become `ps3-ray-chess.*`.

## Running

- **RPCS3:** boot `ps3-ray-chess.self` directly, or *File → Install .pkg* → pick the `.pkg` → launch **RayChess**.
- **Real PS3 (HEN/CFW):** install the `.pkg` from the XMB, or `ps3load` the `.self`.

**Menus.** The game opens on a **main menu** — *Nueva Partida*, *Reanudar Partida*, *Cargar partida*,
*Opciones*, *Salir*. In-game, **Start** opens the **pause menu** (*Reanudar juego en curso*, *Guardar
partida*, *Cargar partida*, *Cambiar equipo*, *Auto-invertir*, *Reiniciar partida*, *Salir a menú
principal*). **Select does nothing.** Quit to the XMB from the main menu's **Salir**. *Opciones* holds
the Fischer time control (*Sin reloj / 3|2 / 5|3 / 10|5*; running out of time loses), which colour is
**Jugador 1** (info-bar label), and **auto-flip**. Menu navigation: D-pad up/down move, left/right
change a value, **Cross** activates, **Circle** backs out.

**Save / load games (XMB Saved Data Utility).** *Guardar partida* / *Cargar partida* use the PS3's
own **Saved Data Utility** — the OS slot picker with icon and title/subtitle/detail, visible in the
console's *Saved Data* manager. A save captures the **full state and complete move history**, so a
game resumes exactly (L1/R1 still work). Settings (*Opciones*) are saved separately and silently.

**Multi-controller.** *Nueva Partida* (and *Cambiar equipo* in the pause menu) opens a controller
screen listing the **powered-on pads** (up to 4). Assign each pad to **Blancas** or **Negras** — on a
side's turn, only that side's pads can move. **A side with no assigned pad is controlled by any pad**,
so a single controller drives both sides (as before); pads that aren't detected are noted on the screen.

**In-game controls:**

| Button | Action |
|--------|--------|
| D-pad / left stick | move the cursor over the squares |
| **Cross** | select a piece / play the move on the cursor square |
| **Circle** | cancel the selection |
| **Triangle** | flip the board view |
| **L1 / R1** | step back / forward through the move history (no confirmation; playing a move from a past position starts a new line) |
| **Start** | open the pause menu |
| in a promotion | **Left/Right** pick Queen/Rook/Bishop/Knight, **Cross** confirms |

## Project structure

```
ps3-ray-chess/
├── .github/workflows/   # CI: build (raylib toolchain image) + docs link lint
├── source/              # ray-chess engine (Board / pieces / Renderer / Game logic) reused as-is,
│                        #   plus the PS3 I/O layer (embedded-asset loading + Camera2D centering in
│                        #   Game.cpp) and compat.h (std::to_string shim)
├── data/                # bin2o-embedded assets: 16 piece PNGs + 4 move-indicator PNGs
│                        #   + click/clickcancel .bin (the original WAVs, used in M3)
├── pkgfiles/            # PKG payload: ICON0.PNG
├── .claude/skills/      # Submodule: ps3-homebrew patterns, as Claude skills
├── scripts/             # Dockerized build.sh / deploy.sh wrappers (raylib image)
├── Makefile             # PSL1GHT build, LD forced to the C++ driver (RSXGL underneath)
├── sfo.xml              # App metadata (TITLE_ID: RAYCHESS)
└── README.md
```

## Roadmap

- **M1 — pipeline** *(done)*: scaffold on the raylib toolchain; the 20 PNGs embedded via `bin2o`
  and loaded from memory; the board + all pieces render centered at 1280×720; boots clean on RPCS3.
- **M2 — gamepad play** *(done)*: a cursor over the squares (D-pad/stick), **Cross** to select/move,
  **Circle** to cancel, and a gamepad promotion menu — driving the engine's existing rules (castling,
  en passant, promotion, check / checkmate / stalemate) to a fully playable game. Edge-triggered
  cursor movement; the cursor + selected square are highlighted.
- **M3 — audio + polish** *(done)*: the original `click` / `clickCancel` SFX via MikMod
  (`source/audio.c` — `Sample_LoadGeneric` over the embedded WAV bytes), played on
  select / move / invalid / promotion; the stalemate end screen now renders; and the board
  coordinates read as standard algebra (files a→h left to right, rank 1 at white's home row).
- **Extras** *(done)*: Fischer time controls, Jugador 1/2 labels, auto-flip; **board flip** on
  Triangle; **move history** on L1/R1 with branch-on-new-move (undo/redo over full-position
  snapshots); a per-side **Fischer clock** (flag-fall loses); synthesized **check** and
  **checkmate/victory** cues.
- **Menus + multi-controller** *(done)*: a **main menu** (Nueva Partida / Reanudar / Opciones /
  Salir) and an in-game **pause menu** (Start); the former settings moved to *Opciones*. Up to **4
  controllers**: assign each pad to a side (only that side's pads move it; a side with no pad is
  driven by any pad, so one controller still runs the whole game).
- **Settings persistence** *(done)*: *Opciones* (time control, Jugador 1 colour, auto-flip) are saved
  to `/dev_hdd0/RAYCHESS/settings.bin` and restored on launch — `source/settings.c` writes it via the
  lv2 filesystem syscalls (silent, no dialog).
- **Save / load games** *(done)*: *Guardar partida* / *Cargar partida* via the XMB **Saved Data
  Utility** (`source/savegame.c` — `sysSaveListSave2/Load2` on a background thread, memory container,
  list/status/file callbacks, ICON0). A save serializes the full state + move history
  (`Game::SerializeGame`), so a game resumes exactly. The save appears in the PS3's *Saved Data* manager.
- **Board UI** *(done)*: the letterbox margins host a **move list** in algebraic notation (left) and
  **captured pieces** with a material-advantage readout (right); the **last move's** from/to squares
  are highlighted. All three follow L1/R1 history and ride along with saves (per-snapshot SAN +
  captured piece, save format v2). (SAN omits disambiguation for the rare two-pieces-same-square case.)
- **4-player chess** *(done)*: a full second mode (chess.com 4PC) in a **self-contained
  `source/four/` engine** (14×14 board, four 3×3 corners removed, four armies tinted red/blue/yellow/
  green from the white silhouettes), routed via a **mode-select menu** (*Clásico* / *4 jugadores FFA*
  / *Equipos*). Legal move generation for all pieces on the masked board (4 pawn directions,
  double-step, **castling** per-side, **en passant**, a **promotion menu**), own-king-safe filtering,
  **turn order** Red→Blue→Yellow→Green, and **per-colour controller assignment** (single-pad
  fallback). **Check / checkmate / stalemate → elimination** (dead grey pieces) with win conditions
  (Teams: first team to mate; FFA: last player standing), the full **FFA points system** (capture
  values, +20 mate/stalemate, multi-check bonuses, winner by points, final standings), a 4-player HUD
  (points / turn / check / eliminated), and **save/load** via the same XMB Saved Data Utility (its own
  blob format, auto-detected on load). The classic game is untouched.
- **e2e test harness** *(done)*: an **opt-in** (`NETTEST`) TCP command server (`source/nettest/`) lets
  a PC drive the game over the network and assert its behaviour automatically instead of checking each
  rule by hand on the console. A line protocol is served on a background thread and applied on the main
  loop — driving (`newgame` / `move` / `promote` / `press`), **position setup** for scenarios
  (`setup <fen>` classic, `setup4` for 4PC, `setclock` for the Fischer clock) and queries (`state` /
  `board` / `legal` / `at` / `history` / `points` / `result`). A **Python + pytest** suite in `tests/`
  (45 tests) covers classic legality, captures, check / checkmate (Fool's, Scholar's, back-rank,
  ladder) / **stalemate**, castling, en passant, promotion, **clock flag-fall**, and — for 4PC — turn
  order, **capture points**, **checkmate/stalemate elimination + scoring**, **multi-check bonuses**,
  and **Teams 2v2 win/draw**, plus synthetic-input menu navigation. Build it with
  `./scripts/build-test.sh` (which cleans first; the shipped `build.sh` PKG opens no port); see
  [`tests/README.md`](tests/README.md).

## Credits & license

- Original game: **[ray-chess](https://github.com/GustavoHenriqueMuller/ray-chess)** by
  **Gustavo Henrique Müller** — **MIT**. Piece sprites, move indicators and sounds in `data/` are
  from ray-chess.
- PS3 scaffold & toolchain: [02900/ps3-toolchain](https://github.com/02900/ps3-toolchain) (raylib
  variant), [02900/ps3-boilerplate-raylib](https://github.com/02900/ps3-boilerplate-raylib).
- [raylib](https://github.com/raysan5/raylib) by Ramon Santamaria; PS3 port
  [nbe1233/raylib-ps3](https://github.com/nbe1233/raylib-ps3) over
  [RSXGL](https://github.com/gzorin/RSXGL).

This port keeps ray-chess's **MIT** license — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).
