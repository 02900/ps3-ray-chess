#include "FourBoard.h"

bool FourBoard::Playable(int i, int j) {
    if (i < 0 || i >= N || j < 0 || j >= N) return false;
    // The four 3x3 corners (rows/cols 0..2 or 11..13 on both axes) are cut out.
    bool iCorner = (i < 3) || (i >= N - 3);
    bool jCorner = (j < 3) || (j >= N - 3);
    return !(iCorner && jCorner);
}

void FourBoard::PlaceArmy(PColor color) {
    // Back rank order along the middle 8 files/ranks (indices 3..10), from the
    // edge's start: R N B Q K B N R.
    static const PIECE_TYPE back[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };

    for (int k = 0; k < 8; k++) {
        int idx = 3 + k;              // 3..10
        int bi, bj, pi, pj;           // back-rank square, pawn square

        switch (color) {
            case P_RED:    bi = 13;  bj = idx; pi = 12;  pj = idx; break;  // bottom, advance -i
            case P_YELLOW: bi = 0;   bj = idx; pi = 1;   pj = idx; break;  // top,    advance +i
            case P_BLUE:   bi = idx; bj = 0;   pi = idx; pj = 1;   break;  // left,   advance +j
            case P_GREEN:  bi = idx; bj = 13;  pi = idx; pj = 12;  break;  // right,  advance -j
            default: return;
        }

        grid[bi][bj] = { true, color, back[k], false, true };
        grid[pi][pj] = { true, color, PEON,    false, true };
    }
}

void FourBoard::Init() {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            grid[i][j] = { false, P_NONE, PEON, false, false };

    PlaceArmy(P_RED);
    PlaceArmy(P_BLUE);
    PlaceArmy(P_YELLOW);
    PlaceArmy(P_GREEN);
}
