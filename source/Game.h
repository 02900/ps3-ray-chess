#ifndef RAY_CHESS_GAME_H
#define RAY_CHESS_GAME_H

#include <string>
#include <map>
#include <vector>

#include "pieces/Piece.h"
#include "Board.h"
#include "raylib.h"
#include "Move.h"

enum GAME_STATE {
    S_RUNNING,
    S_PROMOTION,
    S_WHITE_WINS,
    S_BLACK_WINS,
    S_STALEMATE
};

class Game {
public:
    // PS3 output resolution. The original 640x672 game is drawn into a virtual
    // "canvas" of those dimensions and centered on the 1280x720 screen via a
    // Camera2D offset (see Run()), so the Renderer's layout math is untouched.
    const static int SCREEN_WIDTH = 1280;
    const static int SCREEN_HEIGHT = 720;

    const static int INFO_BAR_HEIGHT = 32;
    const static int WINDOW_WIDTH = 640;                             // canvas width
    const static int WINDOW_HEIGHT = WINDOW_WIDTH + INFO_BAR_HEIGHT; // canvas height (672)
    const static int CELL_SIZE = WINDOW_WIDTH / 8;

    // Offset that centers the 640x672 canvas on the 1280x720 screen.
    const static int BOARD_OFFSET_X = (SCREEN_WIDTH - WINDOW_WIDTH) / 2;    // 320
    const static int BOARD_OFFSET_Y = (SCREEN_HEIGHT - WINDOW_HEIGHT) / 2;  // 24

    const static Color LIGHT_SHADE;
    const static Color DARK_SHADE;

    // Time-control presets (Fischer: base minutes | increment seconds).
    struct TimeControl { const char* label; int baseSec; int incSec; };
    const static TimeControl TIME_CONTROLS[];
    const static int TIME_CONTROL_COUNT;

    Game();
    ~Game();

    void Run();
    void SwapTurns();

private:
    void LoadTextures();

    void UpdateCursor();       // move the board cursor from the D-pad / left stick
    void HandleInput();
    void HandleInputPromotion();
    void HandleMenuInput();
    Move* GetMoveAtPosition(const Position& position);
    void DoMoveOnBoard(const Move& move);

    void CalculateAllPossibleMovements();
    void CheckForEndOfGame();
    void FilterMovesThatAttackOppositeKing();
    void FilterMovesThatLeadToCheck();
    bool IsAnyMovePossible();

    // Match setup / flow.
    void Reset();               // (re)start a game from the initial position
    void ApplyTimeControl();    // load the selected preset into the clocks
    void UpdateClocks();        // tick the side to move; flag-fall ends the game
    void ToggleFlip();          // flip the board view (Triangle)
    void ApplyAutoFlip();       // keep the side to move at the bottom

    // Move history (L1 back / R1 forward). A snapshot is the full position; making a
    // move from a past node truncates the future (branch), per the request.
    struct PieceState { PIECE_TYPE type; PIECE_COLOR color; int i, j; bool hasMoved; };
    struct Snapshot {
        std::vector<PieceState> pieces;
        Position lastMoved;
        PIECE_COLOR turn;
        int round;
        GAME_STATE state;
        double whiteClock, blackClock;
    };
    Snapshot MakeSnapshot() const;
    void RestoreSnapshot(const Snapshot& snapshot);
    void CaptureSnapshot();     // append current position, dropping any redo branch
    void HistoryBack();
    void HistoryForward();
    bool AtLiveTip() const { return historyIndex == (int) history.size() - 1; }

    // Assets.
    std::map<std::string, Texture> textures;

    // Game state.
    Board board;
    PIECE_COLOR turn = PIECE_COLOR::C_WHITE;
    GAME_STATE state = GAME_STATE::S_RUNNING;

    // Selected piece/possible moves state.
    Piece* selectedPiece = nullptr;
    std::map<Piece*, std::vector<Move>> possibleMovesPerPiece;

    // Gamepad input state (replaces the desktop mouse). The cursor is a board cell;
    // dirPrev holds the previous frame's up/down/left/right so movement is
    // edge-triggered (one square per press). promotionChoice indexes Q/R/B/N.
    Position cursor = {6, 4};
    bool dirPrev[4] = {false, false, false, false};
    int promotionChoice = 0;

    // Settings (edited in the Select menu).
    int timeControlIndex = 0;   // index into TIME_CONTROLS
    bool player1IsWhite = true; // info-bar labels: which colour is "Jugador 1"
    bool autoFlip = false;      // flip the board on every turn change

    // View / menu.
    bool flipped = false;       // black at the bottom when true
    bool menuOpen = false;
    int menuIndex = 0;

    // Clocks (seconds remaining; only used when the preset has a base time).
    bool clockActive = false;
    double whiteClock = 0.0;
    double blackClock = 0.0;

    // Move history.
    std::vector<Snapshot> history;
    int historyIndex = -1;

    // Game information (current round and free-running time when there's no clock).
    int round = 1;
    double time = 0;
};

#endif //RAY_CHESS_GAME_H
