#ifndef RAY_CHESS_FOUR_GAME_H
#define RAY_CHESS_FOUR_GAME_H

// 4-player chess game driver. Owns a FourBoard, the mode (FFA / Teams), the four
// players and the board cursor, and renders the whole 14x14 scene. M1: setup +
// render + a movable cursor. Moves/turns/rules land in M2+.
#include <map>
#include <string>

#include "raylib.h"
#include "FourBoard.h"
#include "../Position.h"

enum FourMode { FOUR_FFA, FOUR_TEAMS };

class FourGame {
public:
    // 14x14 cells of 48px = 672x672, centered on 1280x720 by a Camera2D offset
    // that Game sets for this mode.
    static const int CELL = 48;
    static const int BOARD_PX = FourBoard::N * CELL;   // 672

    FourGame(FourMode mode);

    void MoveCursor(int di, int dj);   // step over playable squares only
    void Render(const std::map<std::string, Texture>& textures, bool active) const;

    FourMode Mode() const { return mode; }

private:
    FourBoard board;
    FourMode mode;
    Position cursor;
    int current;   // whose turn (0..3), used from M2

    struct P4 { PColor color; int team; bool eliminated; int points; };
    P4 players[4];

    void CellOrigin(int i, int j, int& x, int& y) const;  // canvas top-left of cell (i,j)
};

#endif // RAY_CHESS_FOUR_GAME_H
