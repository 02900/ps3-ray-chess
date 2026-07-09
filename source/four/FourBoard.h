#ifndef RAY_CHESS_FOUR_BOARD_H
#define RAY_CHESS_FOUR_BOARD_H

// 4-player chess board: 14x14 with the four 3x3 corners removed (160 playable
// squares). Self-contained — shares only PIECE_TYPE / Position with the classic
// engine. M1 is geometry + starting setup; move generation arrives in M2.
#include "../pieces/PieceEnums.h"
#include "../Position.h"

enum PColor { P_RED, P_BLUE, P_YELLOW, P_GREEN, P_NONE };

struct Piece4 {
    bool present;        // is there a piece on this square
    PColor color;
    PIECE_TYPE type;
    bool hasMoved;
    bool alive;          // false = "dead" (grey) after its owner is eliminated (M3)
};

class FourBoard {
public:
    static const int N = 14;

    FourBoard() { Init(); }

    void Init();  // clear + place the four armies in their starting position

    // A square is playable if it's on the 14x14 grid and not in a 3x3 corner.
    static bool Playable(int i, int j);

    const Piece4& At(int i, int j) const { return grid[i][j]; }
    Piece4& At(int i, int j)             { return grid[i][j]; }

private:
    Piece4 grid[N][N];

    void PlaceArmy(PColor color);  // one edge's 8 back-rank pieces + 8 pawns
};

#endif // RAY_CHESS_FOUR_BOARD_H
