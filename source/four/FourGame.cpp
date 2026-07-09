#include "FourGame.h"
#include "../compat.h"   // compat::to_string (PS3 newlib lacks std::to_string)

// ---- palette --------------------------------------------------------------

static Color TintFor(PColor c) {
    switch (c) {
        case P_RED:    return Color{ 214,  57,  57, 255 };
        case P_BLUE:   return Color{  66, 108, 224, 255 };
        case P_YELLOW: return Color{ 226, 205,  56, 255 };
        case P_GREEN:  return Color{  58, 176,  82, 255 };
        default:       return Color{ 200, 200, 200, 255 };
    }
}
static const Color DEAD_TINT  = Color{ 140, 140, 140, 255 };
static const Color LIGHT_SQ   = Color{ 240, 217, 181, 255 };
static const Color DARK_SQ    = Color{ 181, 136,  99, 255 };

static char TypeChar(PIECE_TYPE t) {
    switch (t) {
        case PEON:   return 'p';
        case ROOK:   return 'r';
        case KNIGHT: return 'n';
        case BISHOP: return 'b';
        case QUEEN:  return 'q';
        case KING:   return 'k';
    }
    return 'p';
}

static const char* NameFor(PColor c) {
    switch (c) {
        case P_RED:    return "Rojo";
        case P_BLUE:   return "Azul";
        case P_YELLOW: return "Amarillo";
        case P_GREEN:  return "Verde";
        default:       return "";
    }
}

// ---- construction ---------------------------------------------------------

FourGame::FourGame(FourMode mode_) : mode(mode_), cursor{7, 7}, current(0) {
    // Turn order Red -> Blue -> Yellow -> Green. Teams: Red+Yellow vs Blue+Green.
    static const PColor order[4]  = { P_RED, P_BLUE, P_YELLOW, P_GREEN };
    static const int    teamOf[4] = { 0,     1,      0,        1       };
    for (int k = 0; k < 4; k++) {
        players[k] = { order[k], teamOf[k], false, 0 };
    }
}

void FourGame::CellOrigin(int i, int j, int& x, int& y) const {
    x = j * CELL;
    y = i * CELL;
}

void FourGame::MoveCursor(int di, int dj) {
    int ni = cursor.i + di, nj = cursor.j + dj;
    if (FourBoard::Playable(ni, nj)) { cursor.i = ni; cursor.j = nj; }
}

// ---- move engine ----------------------------------------------------------

int FourGame::TeamOf(PColor c) const {
    if (mode == FOUR_TEAMS) return (c == P_RED || c == P_YELLOW) ? 0 : 1;
    return (int) c;   // FFA: everyone is their own team
}

bool FourGame::IsEnemy(PColor a, PColor b) const {
    if (a == P_NONE || b == P_NONE || a == b) return false;
    return TeamOf(a) != TeamOf(b);
}

void FourGame::ForwardDir(PColor c, int& di, int& dj) const {
    di = dj = 0;
    switch (c) {
        case P_RED:    di = -1; break;   // bottom -> up
        case P_YELLOW: di = +1; break;   // top -> down
        case P_BLUE:   dj = +1; break;   // left -> right
        case P_GREEN:  dj = -1; break;   // right -> left
        default: break;
    }
}

bool FourGame::IsPromoSquare(PColor c, int i, int j) const {
    // FFA promotes on the 8th rank from each side; Teams on the 11th.
    int rank = (mode == FOUR_FFA) ? 8 : 11;
    switch (c) {
        case P_RED:    return i == 14 - rank;
        case P_YELLOW: return i == rank - 1;
        case P_BLUE:   return j == rank - 1;
        case P_GREEN:  return j == 14 - rank;
        default: return false;
    }
}

static const int ROOK_DIR[4][2]   = {{-1,0},{1,0},{0,-1},{0,1}};
static const int BISHOP_DIR[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static const int KNIGHT_OFF[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
static const int KING_OFF[8][2]   = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};

void FourGame::GenMoves(int i, int j, std::vector<Move4>& out) const {
    const Piece4& p = board.At(i, j);
    if (!p.present) return;
    PColor me = p.color;

    auto rays = [&](const int dir[][2], int n) {
        for (int d = 0; d < n; d++) {
            int ti = i + dir[d][0], tj = j + dir[d][1];
            while (FourBoard::Playable(ti, tj)) {
                const Piece4& t = board.At(ti, tj);
                if (!t.present) { out.push_back({ti, tj, MK_WALK}); }
                else { if (IsEnemy(me, t.color) && t.type != KING) out.push_back({ti, tj, MK_CAPTURE}); break; }
                ti += dir[d][0]; tj += dir[d][1];
            }
        }
    };
    auto steps = [&](const int off[][2], int n) {
        for (int d = 0; d < n; d++) {
            int ti = i + off[d][0], tj = j + off[d][1];
            if (!FourBoard::Playable(ti, tj)) continue;
            const Piece4& t = board.At(ti, tj);
            if (!t.present) out.push_back({ti, tj, MK_WALK});
            else if (IsEnemy(me, t.color) && t.type != KING) out.push_back({ti, tj, MK_CAPTURE});
        }
    };

    switch (p.type) {
        case ROOK:   rays(ROOK_DIR, 4); break;
        case BISHOP: rays(BISHOP_DIR, 4); break;
        case QUEEN:  rays(ROOK_DIR, 4); rays(BISHOP_DIR, 4); break;
        case KNIGHT: steps(KNIGHT_OFF, 8); break;
        case KING:   steps(KING_OFF, 8); break;   // castling in M2b
        case PEON: {
            int di, dj; ForwardDir(me, di, dj);
            int oi = i + di, oj = j + dj;
            if (FourBoard::Playable(oi, oj) && !board.At(oi, oj).present) {
                out.push_back({oi, oj, IsPromoSquare(me, oi, oj) ? MK_PROMO : MK_WALK});
                int ti = i + 2 * di, tj = j + 2 * dj;
                if (!p.hasMoved && FourBoard::Playable(ti, tj) && !board.At(ti, tj).present) {
                    out.push_back({ti, tj, MK_DOUBLE});
                }
            }
            // Diagonal captures: forward ± perpendicular.
            int p1i, p1j, p2i, p2j;
            if (di != 0) { p1i = 0; p1j = -1; p2i = 0; p2j = 1; }
            else         { p1i = -1; p1j = 0; p2i = 1; p2j = 0; }
            int diag[2][2] = { { di + p1i, dj + p1j }, { di + p2i, dj + p2j } };
            for (int d = 0; d < 2; d++) {
                int ti = i + diag[d][0], tj = j + diag[d][1];
                if (!FourBoard::Playable(ti, tj)) continue;
                const Piece4& t = board.At(ti, tj);
                if (t.present && IsEnemy(me, t.color) && t.type != KING) {
                    out.push_back({ti, tj, IsPromoSquare(me, ti, tj) ? MK_PROMO : MK_CAPTURE});
                }
            }
            break;
        }
    }
}

void FourGame::GenAttackSquares(const FourBoard& b, int i, int j, std::vector<Position>& out) const {
    const Piece4& p = b.At(i, j);
    if (!p.present) return;

    auto rays = [&](const int dir[][2], int n) {
        for (int d = 0; d < n; d++) {
            int ti = i + dir[d][0], tj = j + dir[d][1];
            while (FourBoard::Playable(ti, tj)) {
                out.push_back({ti, tj});
                if (b.At(ti, tj).present) break;   // first blocker is attacked
                ti += dir[d][0]; tj += dir[d][1];
            }
        }
    };
    auto steps = [&](const int off[][2], int n) {
        for (int d = 0; d < n; d++) {
            int ti = i + off[d][0], tj = j + off[d][1];
            if (FourBoard::Playable(ti, tj)) out.push_back({ti, tj});
        }
    };

    switch (p.type) {
        case ROOK:   rays(ROOK_DIR, 4); break;
        case BISHOP: rays(BISHOP_DIR, 4); break;
        case QUEEN:  rays(ROOK_DIR, 4); rays(BISHOP_DIR, 4); break;
        case KNIGHT: steps(KNIGHT_OFF, 8); break;
        case KING:   steps(KING_OFF, 8); break;
        case PEON: {
            int di, dj; ForwardDir(p.color, di, dj);
            int p1i, p1j, p2i, p2j;
            if (di != 0) { p1i = 0; p1j = -1; p2i = 0; p2j = 1; }
            else         { p1i = -1; p1j = 0; p2i = 1; p2j = 0; }
            int d1i = i + di + p1i, d1j = j + dj + p1j;
            int d2i = i + di + p2i, d2j = j + dj + p2j;
            if (FourBoard::Playable(d1i, d1j)) out.push_back({d1i, d1j});
            if (FourBoard::Playable(d2i, d2j)) out.push_back({d2i, d2j});
            break;
        }
    }
}

bool FourGame::KingAttacked(const FourBoard& b, PColor c) const {
    int ki = -1, kj = -1;
    for (int i = 0; i < FourBoard::N && ki < 0; i++)
        for (int j = 0; j < FourBoard::N; j++) {
            const Piece4& p = b.At(i, j);
            if (p.present && p.color == c && p.type == KING) { ki = i; kj = j; break; }
        }
    if (ki < 0) return false;

    std::vector<Position> atk;
    for (int i = 0; i < FourBoard::N; i++)
        for (int j = 0; j < FourBoard::N; j++) {
            const Piece4& p = b.At(i, j);
            if (!p.present || !p.alive || !IsEnemy(c, p.color)) continue;
            atk.clear();
            GenAttackSquares(b, i, j, atk);
            for (const Position& s : atk) if (s.i == ki && s.j == kj) return true;
        }
    return false;
}

void FourGame::LegalMoves(int i, int j, std::vector<Move4>& out) const {
    std::vector<Move4> pseudo;
    GenMoves(i, j, pseudo);
    PColor me = board.At(i, j).color;

    for (const Move4& m : pseudo) {
        FourBoard copy = board;   // Piece4 is POD, trivially copyable
        Piece4 mover = copy.At(i, j);
        copy.At(i, j) = { false, P_NONE, PEON, false, false };
        if (m.kind == MK_PROMO) mover.type = QUEEN;
        mover.hasMoved = true;
        copy.At(m.i, m.j) = mover;   // captures by overwrite
        if (!KingAttacked(copy, me)) out.push_back(m);
    }
}

void FourGame::ApplyMove(int fi, int fj, const Move4& m) {
    Piece4 mover = board.At(fi, fj);
    board.At(fi, fj) = { false, P_NONE, PEON, false, false };
    if (m.kind == MK_PROMO) mover.type = QUEEN;   // auto-queen (choice menu is M2b)
    mover.hasMoved = true;
    board.At(m.i, m.j) = mover;
    NextTurn();
}

void FourGame::NextTurn() {
    current = (current + 1) % 4;   // eliminated-player skipping is M3
}

void FourGame::Select() {
    const Piece4& p = board.At(cursor.i, cursor.j);
    if (hasSelected) {
        for (const Move4& m : selMoves) {
            if (m.i == cursor.i && m.j == cursor.j) {
                ApplyMove(selected.i, selected.j, m);
                hasSelected = false; selMoves.clear();
                return;
            }
        }
        // Not a legal target: reselect an own piece, else clear.
        if (p.present && p.color == CurrentColor()) {
            selected = cursor; selMoves.clear(); LegalMoves(cursor.i, cursor.j, selMoves);
        } else {
            hasSelected = false; selMoves.clear();
        }
    } else if (p.present && p.color == CurrentColor()) {
        selected = cursor; hasSelected = true;
        selMoves.clear(); LegalMoves(cursor.i, cursor.j, selMoves);
    }
}

void FourGame::Cancel() {
    hasSelected = false;
    selMoves.clear();
}

// ---- rendering ------------------------------------------------------------

void FourGame::Render(const std::map<std::string, Texture>& textures, bool active) const {
    // Board squares (playable only; corners left as the dark letterbox).
    for (int i = 0; i < FourBoard::N; i++) {
        for (int j = 0; j < FourBoard::N; j++) {
            if (!FourBoard::Playable(i, j)) continue;
            int x, y; CellOrigin(i, j, x, y);
            DrawRectangle(x, y, CELL, CELL, ((i + j) % 2 == 0) ? LIGHT_SQ : DARK_SQ);
        }
    }

    // Selected piece's square, under the pieces.
    if (active && hasSelected) {
        int x, y; CellOrigin(selected.i, selected.j, x, y);
        DrawRectangle(x, y, CELL, CELL, Color{ 80, 160, 255, 120 });
    }

    // Pieces: tint the white silhouettes per player colour (grey when dead).
    for (int i = 0; i < FourBoard::N; i++) {
        for (int j = 0; j < FourBoard::N; j++) {
            const Piece4& p = board.At(i, j);
            if (!p.present) continue;
            std::string key = std::string("w") + TypeChar(p.type);
            auto it = textures.find(key);
            if (it == textures.end()) continue;

            int x, y; CellOrigin(i, j, x, y);
            Rectangle src = { 0, 0, (float) it->second.width, (float) it->second.height };
            Rectangle dst = { (float) x, (float) y, (float) CELL, (float) CELL };
            DrawTexturePro(it->second, src, dst, (Vector2){ 0, 0 }, 0.0f,
                           p.alive ? TintFor(p.color) : DEAD_TINT);
        }
    }

    // Legal-move markers for the selected piece (on top of the pieces).
    if (active && hasSelected) {
        for (const Move4& m : selMoves) {
            int x, y; CellOrigin(m.i, m.j, x, y);
            if (m.kind == MK_CAPTURE || m.kind == MK_PROMO) {
                Rectangle r = { (float) x, (float) y, (float) CELL, (float) CELL };
                DrawRectangleLinesEx(r, 3, Color{ 40, 40, 40, 160 });
            } else {
                DrawCircle(x + CELL / 2, y + CELL / 2, 7, Color{ 40, 40, 40, 150 });
            }
        }
    }

    // Cursor.
    if (active) {
        int x, y; CellOrigin(cursor.i, cursor.j, x, y);
        Rectangle r = { (float) x, (float) y, (float) CELL, (float) CELL };
        DrawRectangleLinesEx(r, 3, Color{ 255, 205, 0, 255 });
    }

    // Player HUD stubs in the letterbox margins (canvas x<0 and x>BOARD_PX map to
    // the left/right of the centered board). Red+Blue left, Yellow+Green right.
    struct Slot { PColor c; int x, y; };
    Slot slots[4] = {
        { P_RED,    -292,  40 },
        { P_BLUE,   -292, 210 },
        { P_YELLOW, BOARD_PX + 22,  40 },
        { P_GREEN,  BOARD_PX + 22, 210 },
    };
    for (const Slot& s : slots) {
        int pIdx = 0;
        for (int k = 0; k < 4; k++) if (players[k].color == s.c) pIdx = k;

        DrawRectangle(s.x, s.y, 270, 150, Color{ 24, 24, 28, 220 });
        bool isCurrent = active && (current == pIdx);
        Rectangle border = { (float) s.x, (float) s.y, 270, 150 };
        DrawRectangleLinesEx(border, isCurrent ? 3 : 1,
                             isCurrent ? Color{255,205,0,255} : Color{80,80,90,255});

        DrawRectangle(s.x + 14, s.y + 16, 28, 28, TintFor(s.c));
        DrawText(NameFor(s.c), s.x + 52, s.y + 18, 24, WHITE);
        std::string pts = "Puntos: " + compat::to_string(players[pIdx].points);
        DrawText(pts.c_str(), s.x + 14, s.y + 60, 20, Color{210,210,210,255});
        if (mode == FOUR_TEAMS) {
            std::string tm = std::string("Equipo ") + (players[pIdx].team == 0 ? "A" : "B");
            DrawText(tm.c_str(), s.x + 14, s.y + 90, 18, Color{170,170,180,255});
        }
        if (isCurrent) DrawText("Turno", s.x + 14, s.y + 118, 18, Color{255,205,0,255});
    }
}
