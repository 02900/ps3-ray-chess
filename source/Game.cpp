#include "Game.h"
#include "Position.h"
#include "raylib.h"
#include "Renderer.h"
#include "audio.h"
#include "settings.h"
#include "savegame.h"
#include "compat.h"  // compat::to_string (PS3 newlib lacks std::to_string)

#include <cstring>   // memcpy for game-state (de)serialization
#include "pieces/Queen.h"
#include "pieces/Knight.h"
#include "pieces/Bishop.h"
#include "pieces/Rook.h"

// Assets are embedded in the .self via bin2o (see the Makefile %.png.o rule) and
// loaded from memory below, so the desktop build's runtime std::filesystem scan of
// assets/ is gone. <filesystem> is also unavailable in the PS3 toolchain's libstdc++.

const Color Game::LIGHT_SHADE = Color{240, 217, 181, 255};
const Color Game::DARK_SHADE = Color{181, 136, 99, 255};

// Fischer time-control presets (base seconds | increment seconds). Index 0 = no clock.
const Game::TimeControl Game::TIME_CONTROLS[] = {
    { "Sin reloj",   0, 0 },
    { "3 | 2",     180, 2 },
    { "5 | 3",     300, 3 },
    { "10 | 5",    600, 5 },
};
const int Game::TIME_CONTROL_COUNT = 4;

Game::Game() {
    // Open the framebuffer at the PS3 output resolution; the 640x672 game canvas is
    // centered inside it with a Camera2D offset (see Run()).
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RayChess");
    SetTargetFPS(60);

    // The original click/cancel SFX play through MikMod (raylib's audio device is
    // unused on this RSXGL stack). Defensive: a no-op if init fails.
    audio_init();

    LoadTextures();

    // Restore persisted Opciones before Reset(), so the initial clocks reflect the
    // saved time control.
    LoadSettings();

    // Set up the initial position, clocks and history.
    Reset();
}

// bin2o turns each data/<name>.png into `extern const unsigned char <name>_png[]`
// plus `extern const unsigned int <name>_png_size`. The map keys must match what the
// engine asks for: Piece::GetName() ("wp".."bk") and the move-indicator names.
extern "C" {
    #define DECL_PNG(n) extern const unsigned char n##_png[]; extern const unsigned int n##_png_size;
    DECL_PNG(wp) DECL_PNG(wr) DECL_PNG(wn) DECL_PNG(wb) DECL_PNG(wq) DECL_PNG(wk)
    DECL_PNG(bp) DECL_PNG(br) DECL_PNG(bn) DECL_PNG(bb) DECL_PNG(bq) DECL_PNG(bk)
    DECL_PNG(move) DECL_PNG(castling) DECL_PNG(enpassant) DECL_PNG(promotion)
    DECL_PNG(saveicon)   // ICON0 for the XMB save (not a board texture)
    #undef DECL_PNG
}

void Game::LoadTextures() {
    struct Embedded { const char* key; const unsigned char* data; const unsigned int* size; };
    #define TEX(n) { #n, n##_png, &n##_png_size }
    static const Embedded assets[] = {
        TEX(wp), TEX(wr), TEX(wn), TEX(wb), TEX(wq), TEX(wk),
        TEX(bp), TEX(br), TEX(bn), TEX(bb), TEX(bq), TEX(bk),
        TEX(move), TEX(castling), TEX(enpassant), TEX(promotion),
    };
    #undef TEX

    for (const Embedded& a : assets) {
        Image image = LoadImageFromMemory(".png", a.data, (int) *a.size);
        ImageResize(&image, CELL_SIZE, CELL_SIZE);
        textures[a.key] = LoadTextureFromImage(image);
        UnloadImage(image);
    }
}

Game::~Game() {
    // Free textures.
    for (auto const& kv : textures) {
        UnloadTexture(kv.second);
    }

    board.Clear();

    audio_shutdown();
    CloseWindow();
}

void Game::Run() {
    // Center the 640x672 game canvas on the 1280x720 screen. Every Renderer draw is
    // issued in canvas coordinates and shifted by this offset, so the Renderer's
    // layout math stays exactly as in the desktop game.
    Camera2D camera = {0};
    camera.offset = (Vector2){ (float) BOARD_OFFSET_X, (float) BOARD_OFFSET_Y };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    while (!WindowShouldClose()){
        audio_update();  // drive MikMod's software mixer

        // Per-screen input.
        switch (screen) {
            case SCREEN::SCR_GAME:     HandleGameFrame();  break;
            case SCREEN::SCR_MAIN:     HandleMainMenu();    break;
            case SCREEN::SCR_PAUSE:    HandlePauseMenu();   break;
            case SCREEN::SCR_OPTIONS:  HandleOptionsMenu(); break;
            case SCREEN::SCR_ASSIGN:   HandleAssignMenu();  break;
            case SCREEN::SCR_SAVEBUSY: HandleSaveBusy();    break;
        }
        if (quitRequested) break;  // main-menu "Salir"

        // Render.
        Renderer::SetFlipped(flipped);
        BeginDrawing();
        ClearBackground(Color{25, 25, 28, 255});  // letterbox around the centered board
        BeginMode2D(camera);
        {
            bool inGame = (screen == SCREEN::SCR_GAME);

            std::vector<Move> movesOfSelectedPiece;
            if (selectedPiece) {
                movesOfSelectedPiece = possibleMovesPerPiece.at(selectedPiece);
            }

            // The board scene is always drawn as a backdrop; the play affordances
            // (selection, move dots, cursor, promotion/end overlays) show only in-game.
            Renderer::RenderBackground();

            if (inGame && state == GAME_STATE::S_RUNNING && selectedPiece != nullptr) {
                Renderer::RenderSelection(selectedPiece->GetPosition());
            }

            Renderer::RenderPieces(board, textures);

            if (inGame && state != GAME_STATE::S_PROMOTION) {
                Renderer::RenderMovesSelectedPiece(textures, movesOfSelectedPiece);
            }

            if (inGame && state == GAME_STATE::S_RUNNING) {
                Renderer::RenderCursor(cursor);
            }

            Renderer::RenderGuideText();
            Renderer::RenderInfoBar(round, time, clockActive, whiteClock, blackClock, player1IsWhite);

            if (inGame && state == GAME_STATE::S_PROMOTION) {
                Renderer::RenderPromotionScreen(textures, selectedPiece->color);
                Renderer::RenderPromotionCursor(promotionChoice);
            }

            if (inGame && (state == GAME_STATE::S_WHITE_WINS ||
                           state == GAME_STATE::S_BLACK_WINS ||
                           state == GAME_STATE::S_STALEMATE)) {
                Renderer::RenderEndScreen(state);
            }

            // Menu overlays.
            if (screen == SCREEN::SCR_MAIN) {
                std::vector<std::string> lines = {
                    "Nueva Partida",
                    std::string("Reanudar Partida") + (hasGame ? "" : " (sin partida)"),
                    "Cargar partida",
                    "Opciones",
                    "Salir",
                };
                Renderer::RenderMenu("RayChess", lines, menuIndex, "");
            } else if (screen == SCREEN::SCR_PAUSE) {
                std::vector<std::string> lines = {
                    "Reanudar juego en curso",
                    "Guardar partida",
                    "Cargar partida",
                    "Cambiar equipo",
                    "Auto-invertir: " + std::string(autoFlip ? "Si" : "No"),
                    "Reiniciar partida",
                    "Salir a menu principal",
                };
                Renderer::RenderMenu("Pausa", lines, menuIndex, "");
            } else if (screen == SCREEN::SCR_SAVEBUSY) {
                std::vector<std::string> lines;  // no options; the XMB dialog owns input
                Renderer::RenderMenu(saveBusyIsLoad ? "Cargando partida..." : "Guardando partida...",
                                     lines, -1, "Segui las instrucciones del sistema.");
            } else if (screen == SCREEN::SCR_OPTIONS) {
                std::vector<std::string> lines = {
                    "Ritmo: " + std::string(TIME_CONTROLS[timeControlIndex].label),
                    "Jugador 1: " + std::string(player1IsWhite ? "Blancas" : "Negras"),
                    "Auto-invertir: " + std::string(autoFlip ? "Si" : "No"),
                    "Volver",
                };
                Renderer::RenderMenu("Opciones", lines, menuIndex, "");
            } else if (screen == SCREEN::SCR_ASSIGN) {
                int pads[4];
                int n = listAvailablePads(pads);
                std::vector<std::string> lines;
                for (int k = 0; k < n; k++) {
                    lines.push_back("Mando " + compat::to_string(pads[k] + 1) + ": " +
                                    (padSide[pads[k]] == PIECE_COLOR::C_WHITE ? "Blancas" : "Negras"));
                }
                lines.push_back(assignReturnsToGame ? "Aceptar" : "Comenzar");

                std::string footer = "Detectados: " + compat::to_string(n) +
                    "/4. Un bando sin mando lo controla cualquiera.";
                Renderer::RenderMenu(assignReturnsToGame ? "Cambiar equipo" : "Seleccion de mandos",
                                     lines, menuIndex, footer);
            }
        }
        EndMode2D();
        EndDrawing();
    }
}

// --- Per-screen frame handlers ---------------------------------------------

void Game::HandleGameFrame() {
    // START opens the pause menu; Select does nothing.
    if (padPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT, false)) {
        screen = SCREEN::SCR_PAUSE;
        menuIndex = 0;
        return;
    }

    // Triangle flips the board; L1/R1 step through history (any pad, even after game over).
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP, false)) ToggleFlip();
    if (padPressed(GAMEPAD_BUTTON_LEFT_TRIGGER_1, false))  HistoryBack();
    if (padPressed(GAMEPAD_BUTTON_RIGHT_TRIGGER_1, false)) HistoryForward();

    if (state == GAME_STATE::S_RUNNING) {
        HandleInput();

        // The clock only runs on the live position (not while reviewing history).
        if (clockActive && AtLiveTip()) {
            UpdateClocks();
        } else if (!clockActive) {
            time += GetFrameTime();  // free-running counter when there's no clock
        }
    } else if (state == GAME_STATE::S_PROMOTION) {
        HandleInputPromotion();
    }
}

// Shared menu navigation: returns the row delta / actions via out-params.
void Game::HandleMainMenu() {
    const int COUNT = 5;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP, false))   menuIndex = (menuIndex + COUNT - 1) % COUNT;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN, false)) menuIndex = (menuIndex + 1) % COUNT;

    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, false)) {  // Cross
        if (menuIndex == 0) {                 // Nueva Partida
            assignReturnsToGame = false;
            screen = SCREEN::SCR_ASSIGN;
            menuIndex = 0;
        } else if (menuIndex == 1) {          // Reanudar Partida
            if (hasGame) { screen = SCREEN::SCR_GAME; }
        } else if (menuIndex == 2) {          // Cargar partida
            StartLoadGame();
        } else if (menuIndex == 3) {          // Opciones
            screen = SCREEN::SCR_OPTIONS;
            menuIndex = 0;
        } else {                              // Salir
            quitRequested = true;
        }
    }
}

void Game::HandlePauseMenu() {
    const int COUNT = 7;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP, false))   menuIndex = (menuIndex + COUNT - 1) % COUNT;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN, false)) menuIndex = (menuIndex + 1) % COUNT;

    // Auto-invertir (row 4) toggles with Left/Right on its row.
    if (menuIndex == 4 && (padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT, false) ||
                           padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT, false))) {
        autoFlip = !autoFlip;
        if (autoFlip) ApplyAutoFlip();
        SaveSettings();
    }

    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, false)) {  // Cross
        if (menuIndex == 0) {                 // Reanudar juego en curso
            screen = SCREEN::SCR_GAME;
        } else if (menuIndex == 1) {          // Guardar partida
            StartSaveGame();
        } else if (menuIndex == 2) {          // Cargar partida
            StartLoadGame();
        } else if (menuIndex == 3) {          // Cambiar equipo
            assignReturnsToGame = true;
            screen = SCREEN::SCR_ASSIGN;
            menuIndex = 0;
        } else if (menuIndex == 4) {          // Auto-invertir (also toggles on Cross)
            autoFlip = !autoFlip;
            if (autoFlip) ApplyAutoFlip();
            SaveSettings();
        } else if (menuIndex == 5) {          // Reiniciar partida
            Reset();
            hasGame = true;
            screen = SCREEN::SCR_GAME;
        } else {                              // Salir a menu principal
            screen = SCREEN::SCR_MAIN;
            menuIndex = 0;
        }
    }

    // Circle resumes the game.
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, false)) {
        screen = SCREEN::SCR_GAME;
    }
}

void Game::HandleOptionsMenu() {
    const int COUNT = 4;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP, false))   menuIndex = (menuIndex + COUNT - 1) % COUNT;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN, false)) menuIndex = (menuIndex + 1) % COUNT;

    int delta = 0;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT, false))  delta = -1;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT, false)) delta = +1;

    if (delta != 0) {
        if (menuIndex == 0) {                 // Ritmo
            timeControlIndex = (timeControlIndex + delta + TIME_CONTROL_COUNT) % TIME_CONTROL_COUNT;
            ApplyTimeControl();
        } else if (menuIndex == 1) {          // Jugador 1 colour label
            player1IsWhite = !player1IsWhite;
        } else if (menuIndex == 2) {          // Auto-invertir
            autoFlip = !autoFlip;
            if (autoFlip) ApplyAutoFlip();
        }
        SaveSettings();                       // persist Opciones on every change
    }

    // Cross on "Volver", or Circle, returns to the main menu.
    if ((padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, false) && menuIndex == 3) ||
         padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, false)) {
        screen = SCREEN::SCR_MAIN;
        menuIndex = 0;
    }
}

void Game::HandleAssignMenu() {
    int pads[4];
    int n = listAvailablePads(pads);
    int count = n + 1;  // pad rows + the action row

    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP, false))   menuIndex = (menuIndex + count - 1) % count;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN, false)) menuIndex = (menuIndex + 1) % count;
    if (menuIndex >= count) menuIndex = count - 1;  // pads may have (dis)connected

    // On a pad row, Left/Right toggles that pad's side.
    if (menuIndex < n && (padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT, false) ||
                          padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT, false))) {
        int p = pads[menuIndex];
        padSide[p] = (padSide[p] == PIECE_COLOR::C_WHITE) ? PIECE_COLOR::C_BLACK : PIECE_COLOR::C_WHITE;
    }

    // Cross on the action row.
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, false) && menuIndex == n) {
        if (assignReturnsToGame) {            // Cambiar equipo -> back to the live game
            screen = SCREEN::SCR_GAME;
        } else {                              // Nueva Partida -> start a fresh game
            Reset();
            hasGame = true;
            screen = SCREEN::SCR_GAME;
        }
        menuIndex = 0;
    }

    // Circle backs out (to game if reassigning, else to the main menu).
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, false)) {
        screen = assignReturnsToGame ? SCREEN::SCR_GAME : SCREEN::SCR_MAIN;
        menuIndex = 0;
    }
}

// --- Save / Load via the XMB Saved Data Utility -----------------------------

#define RAYCHESS_SAVE_MAGIC   0x52434731u   /* "RCG1" */
#define RAYCHESS_SAVE_VERSION 1

namespace {
// Little serializers over a byte vector (our own format, read back on the same
// big-endian PPU, so no byte-swapping is needed).
struct Writer {
    std::vector<unsigned char> v;
    void put(const void* p, unsigned n) { const unsigned char* b = (const unsigned char*)p; v.insert(v.end(), b, b + n); }
    void u32(unsigned x)  { put(&x, 4); }
    void i32(int x)       { put(&x, 4); }
    void u8(unsigned char x) { v.push_back(x); }
    void f64(double x)    { put(&x, 8); }
};
struct Reader {
    const unsigned char* p; const unsigned char* end; bool ok;
    Reader(const unsigned char* d, unsigned n) : p(d), end(d + n), ok(true) {}
    void get(void* d, unsigned n) { if (p + n > end) { ok = false; return; } memcpy(d, p, n); p += n; }
    unsigned u32() { unsigned x = 0; get(&x, 4); return x; }
    int i32()      { int x = 0; get(&x, 4); return x; }
    unsigned char u8() { unsigned char x = 0; get(&x, 1); return x; }
    double f64()   { double x = 0; get(&x, 8); return x; }
};
}

std::vector<unsigned char> Game::SerializeGame() const {
    Writer w;
    w.u32(RAYCHESS_SAVE_MAGIC);
    w.u32(RAYCHESS_SAVE_VERSION);

    // Settings / view.
    w.i32(timeControlIndex);
    w.u8(player1IsWhite ? 1 : 0);
    w.u8(autoFlip ? 1 : 0);
    w.u8(flipped ? 1 : 0);
    w.i32(cursor.i);
    w.i32(cursor.j);

    // Live state (clocks tick past the last snapshot, so store them explicitly).
    w.i32((int) turn);
    w.i32(round);
    w.i32((int) state);
    w.f64(whiteClock);
    w.f64(blackClock);

    // Full move history.
    w.i32(historyIndex);
    w.u32((unsigned) history.size());
    for (const Snapshot& s : history) {
        w.i32((int) s.turn);
        w.i32(s.round);
        w.i32((int) s.state);
        w.f64(s.whiteClock);
        w.f64(s.blackClock);
        w.i32(s.lastMoved.i);
        w.i32(s.lastMoved.j);
        w.u32((unsigned) s.pieces.size());
        for (const PieceState& p : s.pieces) {
            w.i32((int) p.type);
            w.i32((int) p.color);
            w.i32(p.i);
            w.i32(p.j);
            w.u8(p.hasMoved ? 1 : 0);
        }
    }
    return w.v;
}

bool Game::DeserializeGame(const unsigned char* data, unsigned size) {
    Reader r(data, size);
    if (r.u32() != RAYCHESS_SAVE_MAGIC) return false;
    if (r.u32() != RAYCHESS_SAVE_VERSION) return false;

    int  tci  = r.i32();
    bool p1w  = r.u8() != 0;
    bool af   = r.u8() != 0;
    bool fl   = r.u8() != 0;
    Position cur; cur.i = r.i32(); cur.j = r.i32();

    PIECE_COLOR lturn = (PIECE_COLOR) r.i32();
    int         lround = r.i32();
    GAME_STATE  lstate = (GAME_STATE) r.i32();
    double lwc = r.f64();
    double lbc = r.f64();

    int hidx = r.i32();
    unsigned hcount = r.u32();

    std::vector<Snapshot> hist;
    for (unsigned k = 0; k < hcount; k++) {
        Snapshot s;
        s.turn  = (PIECE_COLOR) r.i32();
        s.round = r.i32();
        s.state = (GAME_STATE) r.i32();
        s.whiteClock = r.f64();
        s.blackClock = r.f64();
        s.lastMoved.i = r.i32();
        s.lastMoved.j = r.i32();
        unsigned np = r.u32();
        for (unsigned m = 0; m < np; m++) {
            PieceState p;
            p.type  = (PIECE_TYPE) r.i32();
            p.color = (PIECE_COLOR) r.i32();
            p.i = r.i32();
            p.j = r.i32();
            p.hasMoved = r.u8() != 0;
            s.pieces.push_back(p);
        }
        hist.push_back(s);
    }

    if (!r.ok) return false;
    if (hcount == 0 || hidx < 0 || hidx >= (int) hcount) return false;
    if (tci < 0 || tci >= TIME_CONTROL_COUNT) tci = 0;

    // Commit.
    timeControlIndex = tci;
    player1IsWhite = p1w;
    autoFlip = af;
    history = hist;
    historyIndex = hidx;
    RestoreSnapshot(history[historyIndex]);   // rebuilds the board + recomputes moves

    // Override with the saved live values (clocks, view).
    turn = lturn;
    round = lround;
    state = lstate;
    whiteClock = lwc;
    blackClock = lbc;
    clockActive = TIME_CONTROLS[timeControlIndex].baseSec > 0;
    cursor = cur;
    flipped = fl;
    selectedPiece = nullptr;
    return true;
}

void Game::StartSaveGame() {
    std::vector<unsigned char> blob = SerializeGame();

    std::string subtitle = "Ronda " + compat::to_string(round);
    std::string detail = std::string("Turno: ") + (turn == PIECE_COLOR::C_WHITE ? "Blancas" : "Negras")
                       + "\nRitmo: " + TIME_CONTROLS[timeControlIndex].label;

    savegame_start_save(blob.data(), (unsigned) blob.size(),
                        "RayChess", subtitle.c_str(), detail.c_str(),
                        saveicon_png, saveicon_png_size);

    saveBusyIsLoad = false;
    saveReturnScreen = screen;   // came from the pause menu
    screen = SCREEN::SCR_SAVEBUSY;
}

void Game::StartLoadGame() {
    savegame_start_load();
    saveBusyIsLoad = true;
    saveReturnScreen = screen;   // main or pause menu
    screen = SCREEN::SCR_SAVEBUSY;
}

void Game::HandleSaveBusy() {
    int st = savegame_status();
    if (st == SAVEGAME_BUSY) return;   // the XMB dialog owns input while it runs

    if (st == SAVEGAME_OK && saveBusyIsLoad) {
        unsigned sz = 0;
        const void* d = savegame_result_data(&sz);
        bool ok = d && DeserializeGame((const unsigned char*) d, sz);
        savegame_clear();
        if (ok) { hasGame = true; screen = SCREEN::SCR_GAME; }
        else    { screen = saveReturnScreen; }
    } else {
        // Save finished, or the dialog was cancelled/failed: return where we came from.
        savegame_clear();
        screen = saveReturnScreen;
    }
    menuIndex = 0;
}

// --- Multi-controller input -------------------------------------------------

int Game::listAvailablePads(int out[4]) const {
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (IsGamepadAvailable(i)) out[n++] = i;
    }
    return n;
}

bool Game::padCountsForTurn(int i) const {
    if (!IsGamepadAvailable(i)) return false;

    // Does any connected pad belong to the side to move?
    bool sideHasPad = false;
    for (int k = 0; k < 4; k++) {
        if (IsGamepadAvailable(k) && padSide[k] == turn) { sideHasPad = true; break; }
    }
    // If a side has no pad of its own, any connected pad may move it (covers the
    // single-controller case, which then drives both sides).
    return sideHasPad ? (padSide[i] == turn) : true;
}

bool Game::padPressed(int button, bool turnOnly) const {
    for (int i = 0; i < 4; i++) {
        bool include = turnOnly ? padCountsForTurn(i) : IsGamepadAvailable(i);
        if (include && IsGamepadButtonPressed(i, button)) return true;
    }
    return false;
}

bool Game::padDown(int button, bool turnOnly) const {
    for (int i = 0; i < 4; i++) {
        bool include = turnOnly ? padCountsForTurn(i) : IsGamepadAvailable(i);
        if (include && IsGamepadButtonDown(i, button)) return true;
    }
    return false;
}

float Game::padAxis(int axis, bool turnOnly) const {
    float best = 0.0f;
    for (int i = 0; i < 4; i++) {
        bool include = turnOnly ? padCountsForTurn(i) : IsGamepadAvailable(i);
        if (!include) continue;
        float v = GetGamepadAxisMovement(i, axis);
        if ((v < 0 ? -v : v) > (best < 0 ? -best : best)) best = v;
    }
    return best;
}

// Move the board cursor one cell per press from the D-pad or the left stick.
// Edge-triggered (via dirPrev) so a held direction doesn't skate across the board.
// Only the pads that may move the side to move are read.
void Game::UpdateCursor() {
    const float DZ = 0.5f;  // analog dead-zone
    float ax = padAxis(GAMEPAD_AXIS_LEFT_X, true);
    float ay = padAxis(GAMEPAD_AXIS_LEFT_Y, true);

    bool dir[4];
    dir[0] = padDown(GAMEPAD_BUTTON_LEFT_FACE_UP, true)    || ay < -DZ; // screen up
    dir[1] = padDown(GAMEPAD_BUTTON_LEFT_FACE_DOWN, true)  || ay >  DZ; // screen down
    dir[2] = padDown(GAMEPAD_BUTTON_LEFT_FACE_LEFT, true)  || ax < -DZ; // screen left
    dir[3] = padDown(GAMEPAD_BUTTON_LEFT_FACE_RIGHT, true) || ax >  DZ; // screen right

    // Translate screen-space intent into board deltas. When flipped, screen up = higher
    // board row, so both axes invert.
    int di = 0, dj = 0;
    if (dir[0] && !dirPrev[0]) di -= 1;
    if (dir[1] && !dirPrev[1]) di += 1;
    if (dir[2] && !dirPrev[2]) dj -= 1;
    if (dir[3] && !dirPrev[3]) dj += 1;
    if (flipped) { di = -di; dj = -dj; }

    if (cursor.i + di >= 0 && cursor.i + di < 8) cursor.i += di;
    if (cursor.j + dj >= 0 && cursor.j + dj < 8) cursor.j += dj;

    for (int k = 0; k < 4; k++) dirPrev[k] = dir[k];
}

void Game::HandleInput() {
    UpdateCursor();

    // Circle cancels the current selection.
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, true)) {
        selectedPiece = nullptr;
        return;
    }

    // Cross acts on the cursor cell — the same select/move logic the mouse drove.
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, true)) {
        Position clickedPosition = cursor;
        Piece* clickedPiece = board.At(clickedPosition);

        // Select piece.
        if (clickedPiece != nullptr && clickedPiece->color == turn) {
            audio_play_click();
            selectedPiece = clickedPiece;
        } else {
            // Do movement.
            Move* desiredMove = GetMoveAtPosition(clickedPosition);

            if (desiredMove && selectedPiece != nullptr) {
                audio_play_click();
                DoMoveOnBoard(*desiredMove);
            } else {
                audio_play_cancel();
            }

            // Piece must still be selected to render promotion screen.
            if (!desiredMove ||
               (desiredMove->type != MOVE_TYPE::PROMOTION &&
                desiredMove->type != MOVE_TYPE::ATTACK_AND_PROMOTION)
            ) {
                selectedPiece = nullptr;
            }
        }
    }
}

void Game::HandleInputPromotion() {
    // Left/Right move through the four options (Queen, Rook, Bishop, Knight).
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT, true)  && promotionChoice > 0) promotionChoice--;
    if (padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT, true) && promotionChoice < 3) promotionChoice++;

    // Cross confirms the highlighted option.
    if (padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, true)) {
        audio_play_click();
        Position pos = selectedPiece->GetPosition();
        PIECE_COLOR color = selectedPiece->color;
        Piece* newPiece;

        if (promotionChoice == 0) {        // Queen.
            newPiece = new Queen(pos, color);
        } else if (promotionChoice == 1) { // Rook.
            newPiece = new Rook(pos, color);
        } else if (promotionChoice == 2) { // Bishop.
            newPiece = new Bishop(pos, color);
        } else {                           // Knight.
            newPiece = new Knight(pos, color);
        }

        // Destroy peon, create new piece at same position.
        board.Destroy(pos);
        board.Add(newPiece);

        // Quit promotion, deselect peon and swap turns.
        state = GAME_STATE::S_RUNNING;

        selectedPiece = nullptr;
        possibleMovesPerPiece.clear();
        promotionChoice = 0;

        SwapTurns();
    }
}

Move* Game::GetMoveAtPosition(const Position& position) {
    for (auto& [piece, moves] : possibleMovesPerPiece) {
        if (piece == selectedPiece) {
            for (Move& move : moves) {
                if (move.position.i == position.i && move.position.j == position.j) {
                    return &move;
                }
            }
        }
    }
    
    return nullptr;
}

void Game::DoMoveOnBoard(const Move& move) {
    board.DoMove(selectedPiece, move);

    // If the move was a promotion move, show the promotion screen. Else, swap turns.
    if (move.type == MOVE_TYPE::PROMOTION || move.type == MOVE_TYPE::ATTACK_AND_PROMOTION) {
        state = GAME_STATE::S_PROMOTION;
    } else {
        SwapTurns();
    }
}

void Game::SwapTurns() {
    // Fischer increment: the player who just moved (the current `turn`) gets it.
    if (clockActive) {
        int inc = TIME_CONTROLS[timeControlIndex].incSec;
        if (turn == PIECE_COLOR::C_WHITE) whiteClock += inc; else blackClock += inc;
    }

    turn = Piece::GetInverseColor(turn);

    // Advance round.
    if (turn == PIECE_COLOR::C_WHITE) {
        round++;
    }

    // Calculate all possible movements for the current pieces.
    CalculateAllPossibleMovements();

    // Check for stalemates or checkmates. If so, ends the game.
    CheckForEndOfGame();

    // Feedback: victory fanfare on checkmate, alert when the side to move is in check.
    if (state == GAME_STATE::S_WHITE_WINS || state == GAME_STATE::S_BLACK_WINS) {
        audio_play_win();
    } else if (state == GAME_STATE::S_RUNNING && board.IsInCheck(turn)) {
        audio_play_check();
    }

    if (autoFlip) ApplyAutoFlip();

    // Record the resulting position for L1/R1 navigation (drops any redo branch).
    CaptureSnapshot();
}

void Game::CalculateAllPossibleMovements() {
    possibleMovesPerPiece.clear();

    for (Piece* piece : board.GetPiecesByColor(turn)) {
        possibleMovesPerPiece[piece] = piece->GetPossibleMoves(board);
    }

    // Remove the moves that could destroy the opponent's king.
    FilterMovesThatAttackOppositeKing();

    // Remove the moves that cause the player to get in check.
    FilterMovesThatLeadToCheck();
}

void Game::CheckForEndOfGame() {
    std::vector<Piece*> piecesOfCurrentTurn = board.GetPiecesByColor(turn);

    if (board.IsInCheck(turn)) {
        // If there are no moves possible and in check, declare checkmate.
        if (!IsAnyMovePossible()) {
            state = (turn == PIECE_COLOR::C_WHITE ? GAME_STATE::S_BLACK_WINS : GAME_STATE::S_WHITE_WINS);
        }
    } else if (!IsAnyMovePossible()) {
        // If not in check and there is not any move possible, declare stalemate.
        state = GAME_STATE::S_STALEMATE;
    }
}

void Game::FilterMovesThatAttackOppositeKing() {
    for (auto& [piece, possibleMoves] : possibleMovesPerPiece) {
        for (int i = possibleMoves.size() - 1; i >= 0; i--) {
            Move& move = possibleMoves[i];

            // Remove moves that attack the opponent's king.
            bool isAttackMove = move.type == MOVE_TYPE::ATTACK || move.type == MOVE_TYPE::ATTACK_AND_PROMOTION;

            if (isAttackMove) {
                Piece* attackedPiece = board.At(move.position);

                if (attackedPiece->type == PIECE_TYPE::KING && attackedPiece->color != turn) {
                    possibleMoves.erase(possibleMoves.begin() + i);
                }
            }
        }
    }
}

void Game::FilterMovesThatLeadToCheck() {
    for (auto& [piece, possibleMoves] : possibleMovesPerPiece) {
        for (int i = possibleMoves.size() - 1; i >= 0; i--) {
            Move& move = possibleMoves[i];

            // If short castling or long castling, check for intermediary positions between king and rook.
            if (move.type == MOVE_TYPE::SHORT_CASTLING || move.type == MOVE_TYPE::LONG_CASTLING) {
                std::vector<Position> intermediaryPositions;

                if (move.type == MOVE_TYPE::SHORT_CASTLING) {
                    intermediaryPositions = {{piece->GetPosition().i, 5}, {piece->GetPosition().i, 6}};
                } else {
                    intermediaryPositions = {{piece->GetPosition().i, 3}, {piece->GetPosition().i, 2}};
                }

                for (const Position& position : intermediaryPositions) {
                    if (board.MoveLeadsToCheck(piece, {MOVE_TYPE::WALK, position})) {
                        possibleMoves.erase(possibleMoves.begin() + i);
                        break;
                    }
                }

            // If normal move.
            } else if (board.MoveLeadsToCheck(piece, possibleMoves[i])) {
                possibleMoves.erase(possibleMoves.begin() + i);
            }
        }
    }
}

bool Game::IsAnyMovePossible() {
    for (const auto& [pieceName, possibleMoves] : possibleMovesPerPiece) {
        if (!possibleMoves.empty()) {
            return true;
        }
    }

    return false;
}

// --- Match setup / flow -----------------------------------------------------

void Game::Reset() {
    board.Clear();
    board.Init();

    turn = PIECE_COLOR::C_WHITE;
    state = GAME_STATE::S_RUNNING;
    selectedPiece = nullptr;
    possibleMovesPerPiece.clear();
    round = 1;
    time = 0.0;
    promotionChoice = 0;
    cursor = {6, 4};
    flipped = false;

    ApplyTimeControl();
    CalculateAllPossibleMovements();

    // Seed the history with the initial position.
    history.clear();
    historyIndex = -1;
    CaptureSnapshot();

    if (autoFlip) ApplyAutoFlip();
}

void Game::ApplyTimeControl() {
    const TimeControl& tc = TIME_CONTROLS[timeControlIndex];
    clockActive = tc.baseSec > 0;
    whiteClock = tc.baseSec;
    blackClock = tc.baseSec;
}

void Game::LoadSettings() {
    raychess_settings_t s;
    if (settings_load(&s)) {
        if (s.timeControlIndex >= 0 && s.timeControlIndex < TIME_CONTROL_COUNT) {
            timeControlIndex = s.timeControlIndex;
        }
        player1IsWhite = (s.player1IsWhite != 0);
        autoFlip = (s.autoFlip != 0);
    }
}

void Game::SaveSettings() {
    raychess_settings_t s;
    s.timeControlIndex = timeControlIndex;
    s.player1IsWhite = player1IsWhite ? 1 : 0;
    s.autoFlip = autoFlip ? 1 : 0;
    settings_save(&s);
}

void Game::UpdateClocks() {
    double dt = GetFrameTime();

    if (turn == PIECE_COLOR::C_WHITE) {
        whiteClock -= dt;
        if (whiteClock <= 0.0) { whiteClock = 0.0; state = GAME_STATE::S_BLACK_WINS; audio_play_win(); }
    } else {
        blackClock -= dt;
        if (blackClock <= 0.0) { blackClock = 0.0; state = GAME_STATE::S_WHITE_WINS; audio_play_win(); }
    }
}

void Game::ToggleFlip() {
    flipped = !flipped;
}

void Game::ApplyAutoFlip() {
    // Keep the side to move at the bottom of the screen.
    flipped = (turn == PIECE_COLOR::C_BLACK);
}

// --- Move history -----------------------------------------------------------

Game::Snapshot Game::MakeSnapshot() const {
    Snapshot s;
    for (Piece* p : board.GetPiecesByColor(PIECE_COLOR::C_WHITE)) {
        s.pieces.push_back({ p->type, p->color, p->GetPosition().i, p->GetPosition().j, p->HasMoved() });
    }
    for (Piece* p : board.GetPiecesByColor(PIECE_COLOR::C_BLACK)) {
        s.pieces.push_back({ p->type, p->color, p->GetPosition().i, p->GetPosition().j, p->HasMoved() });
    }
    s.lastMoved = board.GetLastMovedPosition();
    s.turn = turn;
    s.round = round;
    s.state = state;
    s.whiteClock = whiteClock;
    s.blackClock = blackClock;
    return s;
}

void Game::RestoreSnapshot(const Snapshot& snapshot) {
    board.Clear();
    for (const PieceState& ps : snapshot.pieces) {
        Piece* p = Piece::CreatePieceByType(ps.type, { ps.i, ps.j }, ps.color);
        p->SetHasMoved(ps.hasMoved);
        board.Add(p);
    }
    board.SetLastMovedPosition(snapshot.lastMoved);

    turn = snapshot.turn;
    round = snapshot.round;
    state = snapshot.state;
    whiteClock = snapshot.whiteClock;
    blackClock = snapshot.blackClock;

    selectedPiece = nullptr;
    promotionChoice = 0;
    CalculateAllPossibleMovements();

    if (autoFlip) ApplyAutoFlip();
}

void Game::CaptureSnapshot() {
    // Making a move from a past node discards the old future (the branch it superseded).
    if (historyIndex < (int) history.size() - 1) {
        history.resize(historyIndex + 1);
    }
    history.push_back(MakeSnapshot());
    historyIndex = (int) history.size() - 1;
}

void Game::HistoryBack() {
    if (historyIndex > 0) {
        historyIndex--;
        RestoreSnapshot(history[historyIndex]);
    }
}

void Game::HistoryForward() {
    if (historyIndex < (int) history.size() - 1) {
        historyIndex++;
        RestoreSnapshot(history[historyIndex]);
    }
}

