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

Game::Game() {
    // Open the framebuffer at the PS3 output resolution; the 640x672 game canvas is
    // centered inside it with a Camera2D offset (see Run()).
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RayChess");
    SetTargetFPS(60);

    // The original click/cancel SFX play through MikMod (raylib's audio device is
    // unused on this RSXGL stack). Defensive: a no-op if init fails.
    audio_init();

    LoadTextures();

    // Init the board and calculate the initial movements for the white player.
    board.Init();
    CalculateAllPossibleMovements();
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

        // Input. (M1: no mouse on PS3, so HandleInput is inert; gamepad cursor is M2.)
        if (state == GAME_STATE::S_RUNNING) {
            HandleInput();

            // Getting new time.
            time += GetFrameTime();
        }

        if (state == GAME_STATE::S_PROMOTION) {
            HandleInputPromotion();
        }

        // Render.
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

            // The gamepad cursor sits on top of the board.
            if (state == GAME_STATE::S_RUNNING) {
                Renderer::RenderCursor(cursor);
            }

            Renderer::RenderGuideText();
            Renderer::RenderInfoBar(round, time);

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
    dir[0] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)    || ay < -DZ; // up
    dir[1] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)  || ay >  DZ; // down
    dir[2] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)  || ax < -DZ; // left
    dir[3] = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax >  DZ; // right

    if (dir[0] && !dirPrev[0] && cursor.i > 0) cursor.i--;
    if (dir[1] && !dirPrev[1] && cursor.i < 7) cursor.i++;
    if (dir[2] && !dirPrev[2] && cursor.j > 0) cursor.j--;
    if (dir[3] && !dirPrev[3] && cursor.j < 7) cursor.j++;

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
    turn = Piece::GetInverseColor(turn);

    // Advance round.
    if (turn == PIECE_COLOR::C_WHITE) {
        round++;
    }

    // Calculate all possible movements for the current pieces.
    CalculateAllPossibleMovements();

    // Check for stalemates or checkmates. If so, ends the game.
    CheckForEndOfGame();
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
