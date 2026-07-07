#include "Game.h"
#include "Position.h"
#include "raylib.h"
#include "Renderer.h"
#include "audio.h"
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
        // Quit to the XMB on Start (WindowShouldClose also catches the system exit).
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
            break;
        }

        audio_update();  // drive MikMod's software mixer

        // Select toggles the settings menu (pauses play while open).
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            menuOpen = !menuOpen;
            menuIndex = 0;
        }

        if (menuOpen) {
            HandleMenuInput();
        } else {
            // Triangle flips the board view. L1 / R1 step back / forward through the
            // move history (no confirmation); these work even after the game ends.
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) ToggleFlip();
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1))  HistoryBack();
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) HistoryForward();

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

        // Render.
        Renderer::SetFlipped(flipped);
        BeginDrawing();
        ClearBackground(Color{25, 25, 28, 255});  // letterbox around the centered board
        BeginMode2D(camera);
        {
            std::vector<Move> movesOfSelectedPiece;

            if (selectedPiece) {
                movesOfSelectedPiece = possibleMovesPerPiece.at(selectedPiece);
            }

            // The 8x8 board + info bar fully cover the canvas, so no per-frame clear
            // of the canvas is needed (Renderer::Clear would repaint the whole screen).
            Renderer::RenderBackground();

            // Highlight the selected piece's square underneath the pieces.
            if (state == GAME_STATE::S_RUNNING && selectedPiece != nullptr) {
                Renderer::RenderSelection(selectedPiece->GetPosition());
            }

            Renderer::RenderPieces(board, textures);

            if (state != GAME_STATE::S_PROMOTION) {
                Renderer::RenderMovesSelectedPiece(textures, movesOfSelectedPiece);
            }

            // The gamepad cursor sits on top of the board (hidden while in the menu).
            if (state == GAME_STATE::S_RUNNING && !menuOpen) {
                Renderer::RenderCursor(cursor);
            }

            Renderer::RenderGuideText();
            Renderer::RenderInfoBar(round, time, clockActive, whiteClock, blackClock, player1IsWhite);

            // Render promotion screen (with the highlighted option).
            if (state == GAME_STATE::S_PROMOTION) {
                Renderer::RenderPromotionScreen(textures, selectedPiece->color);
                Renderer::RenderPromotionCursor(promotionChoice);
            }

            // Render end-game screen (checkmate or stalemate).
            if (state == GAME_STATE::S_WHITE_WINS ||
                state == GAME_STATE::S_BLACK_WINS ||
                state == GAME_STATE::S_STALEMATE) {
                Renderer::RenderEndScreen(state);
            }

            // The settings menu overlays everything.
            if (menuOpen) {
                std::vector<std::string> lines = {
                    "Ritmo: " + std::string(TIME_CONTROLS[timeControlIndex].label),
                    "Jugador 1: " + std::string(player1IsWhite ? "Blancas" : "Negras"),
                    "Auto-invertir: " + std::string(autoFlip ? "Si" : "No"),
                    "Reiniciar partida",
                    "Reanudar",
                };
                Renderer::RenderMenu(lines, menuIndex);
            }
        }
        EndMode2D();
        EndDrawing();
    }
}

// Move the board cursor one cell per press from the D-pad or the left stick.
// Edge-triggered (via dirPrev) so a held direction doesn't skate across the board.
void Game::UpdateCursor() {
    const float DZ = 0.5f;  // analog dead-zone
    float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
    float ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);

    bool dir[4];
    dir[0] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)    || ay < -DZ; // screen up
    dir[1] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)  || ay >  DZ; // screen down
    dir[2] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)  || ax < -DZ; // screen left
    dir[3] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax >  DZ; // screen right

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
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
        selectedPiece = nullptr;
        return;
    }

    // Cross acts on the cursor cell — the same select/move logic the mouse drove.
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
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
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)  && promotionChoice > 0) promotionChoice--;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) && promotionChoice < 3) promotionChoice++;

    // Cross confirms the highlighted option.
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
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

// --- Settings menu ----------------------------------------------------------

void Game::HandleMenuInput() {
    const int MENU_COUNT = 5;

    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))   menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) menuIndex = (menuIndex + 1) % MENU_COUNT;

    int delta = 0;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  delta = -1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) delta = +1;

    if (delta != 0) {
        if (menuIndex == 0) {          // time control
            timeControlIndex = (timeControlIndex + delta + TIME_CONTROL_COUNT) % TIME_CONTROL_COUNT;
            ApplyTimeControl();        // changing the pace resets the clocks
        } else if (menuIndex == 1) {   // which colour is Jugador 1 (labels)
            player1IsWhite = !player1IsWhite;
        } else if (menuIndex == 2) {   // auto-flip
            autoFlip = !autoFlip;
            if (autoFlip) ApplyAutoFlip();
        }
    }

    // Cross activates the action rows.
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
        if (menuIndex == 3) {          // restart
            Reset();
            menuOpen = false;
        } else if (menuIndex == 4) {   // resume
            menuOpen = false;
        }
    }

    // Circle also closes the menu.
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
        menuOpen = false;
    }
}
