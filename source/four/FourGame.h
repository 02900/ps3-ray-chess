#ifndef RAY_CHESS_FOUR_GAME_H
#define RAY_CHESS_FOUR_GAME_H

// 4-player chess game driver. Owns a FourBoard, the mode (FFA / Teams), the four
// players and the board cursor, and renders the whole 14x14 scene. M1: setup +
// render + a movable cursor. Moves/turns/rules land in M2+.
#include <map>
#include <string>
#include <vector>

#include "raylib.h"
#include "FourBoard.h"
#include "../Position.h"

enum FourMode { FOUR_FFA, FOUR_TEAMS };

// A candidate move of the piece at the selected square.
enum MoveKind { MK_WALK, MK_CAPTURE, MK_DOUBLE, MK_PROMO, MK_ENPASSANT, MK_CASTLE };
struct Move4 { int i, j; MoveKind kind; };

class FourGame {
public:
    // 14x14 cells of 48px = 672x672, centered on 1280x720 by a Camera2D offset
    // that Game sets for this mode.
    static const int CELL = 48;
    static const int BOARD_PX = FourBoard::N * CELL;   // 672

    FourGame(FourMode mode);

    void MoveCursor(int di, int dj);   // step over playable squares only
    void Select();                     // Cross: pick up / drop on the cursor square
    void Cancel();                     // Circle: clear the selection
    void Render(const std::map<std::string, Texture>& textures, bool active) const;

    // Promotion picker (Q/R/B/N) while a pawn awaits its choice.
    bool IsPromoPending() const { return promoPending; }
    void PromoMove(int delta);         // Left/Right through the four options
    void ConfirmPromo();               // Cross: apply the chosen piece + advance turn

    FourMode Mode() const { return mode; }
    PColor TurnColor() const { return players[current].color; }   // whose turn it is
    bool IsGameOver() const { return gameOver; }

private:
    FourBoard board;
    FourMode mode;
    Position cursor;
    int current;   // whose turn (index 0..3 into players)

    struct P4 { PColor color; int team; bool eliminated; int points; };
    P4 players[4];

    // Selection state.
    bool hasSelected = false;
    Position selected = {0, 0};
    std::vector<Move4> selMoves;

    // En-passant window: valid only for the ply immediately after a double-step.
    bool epValid = false;
    Position epMid = {0, 0};    // the square the pawn passed over (capture lands here)
    Position epPawn = {0, 0};   // the double-stepped pawn to remove

    // Promotion picker.
    bool promoPending = false;
    Position promoSquare = {0, 0};
    int promoChoice = 0;        // 0..3 = Queen / Rook / Bishop / Knight

    // End-of-game.
    bool gameOver = false;
    std::string resultMsg;

    // Move engine.
    PColor CurrentColor() const { return players[current].color; }
    int TeamOf(PColor c) const;                 // FFA: unique per colour; Teams: 0/1
    bool IsEnemy(PColor a, PColor b) const;     // capturable (different team)
    void ForwardDir(PColor c, int& di, int& dj) const;  // pawn advance vector
    bool IsPromoSquare(PColor c, int i, int j) const;
    void GenMoves(int i, int j, std::vector<Move4>& out) const;             // pseudo-legal
    void AddCastling(int i, int j, std::vector<Move4>& out) const;
    void GenAttackSquares(const FourBoard& b, int i, int j, std::vector<Position>& out) const;
    bool KingAttacked(const FourBoard& b, PColor c) const;
    void LegalMoves(int i, int j, std::vector<Move4>& out) const;           // filtered
    void ApplyMove(int fi, int fj, const Move4& m);
    void AdvanceTurn(int moverIdx);             // next player + resolve mate/stalemate
    bool HasAnyLegalMove(PColor c) const;
    void EliminatePlayer(int idx, bool checkmate);
    void AwardCheckBonus(int moverIdx, PIECE_TYPE movedType);  // FFA multi-check bonus

    void CellOrigin(int i, int j, int& x, int& y) const;  // canvas top-left of cell (i,j)
};

#endif // RAY_CHESS_FOUR_GAME_H
