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
        case KING:   steps(KING_OFF, 8); AddCastling(i, j, out); break;
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
                } else if (!t.present && epValid && ti == epMid.i && tj == epMid.j &&
                           IsEnemy(me, board.At(epPawn.i, epPawn.j).color)) {
                    out.push_back({ti, tj, MK_ENPASSANT});
                }
            }
            break;
        }
    }
}

void FourGame::AddCastling(int i, int j, std::vector<Move4>& out) const {
    const Piece4& king = board.At(i, j);
    if (king.type != KING || king.hasMoved) return;
    PColor c = king.color;
    if (KingAttacked(board, c)) return;   // can't castle out of check

    int di, dj; ForwardDir(c, di, dj);
    bool axisJ = (di != 0);               // king slides along j (horizontal back rank)
    int kc = axisJ ? j : i;               // king coordinate on the sliding axis

    for (int side = 0; side < 2; side++) {
        int rookCoord = (side == 0) ? 3 : 10;
        int dir = (rookCoord > kc) ? +1 : -1;
        int ri = axisJ ? i : rookCoord;
        int rj = axisJ ? rookCoord : j;
        const Piece4& rook = board.At(ri, rj);
        if (!rook.present || rook.color != c || rook.type != ROOK || rook.hasMoved) continue;

        // Squares strictly between king and rook must be empty.
        bool clear = true;
        for (int step = kc + dir; step != rookCoord; step += dir) {
            int si = axisJ ? i : step, sj = axisJ ? step : j;
            if (board.At(si, sj).present) { clear = false; break; }
        }
        if (!clear) continue;

        // The king must not pass through or land on an attacked square.
        bool safe = true;
        for (int s = 1; s <= 2 && safe; s++) {
            int ti = axisJ ? i : (i + dir * s);
            int tj = axisJ ? (j + dir * s) : j;
            FourBoard copy = board;
            copy.At(i, j) = { false, P_NONE, PEON, false, false };
            Piece4 k = king; k.hasMoved = true;
            copy.At(ti, tj) = k;
            if (KingAttacked(copy, c)) safe = false;
        }
        if (!safe) continue;

        int kti = axisJ ? i : (i + dir * 2);
        int ktj = axisJ ? (j + dir * 2) : j;
        out.push_back({ kti, ktj, MK_CASTLE });
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
        if (m.kind == MK_ENPASSANT) copy.At(epPawn.i, epPawn.j) = { false, P_NONE, PEON, false, false };
        if (m.kind == MK_PROMO) mover.type = QUEEN;
        mover.hasMoved = true;
        copy.At(m.i, m.j) = mover;   // captures by overwrite
        // (Castling's rook move doesn't affect the mover's king safety, so the
        //  generic king move here is enough; AddCastling already vetted the path.)
        if (!KingAttacked(copy, me)) out.push_back(m);
    }
}

void FourGame::ApplyMove(int fi, int fj, const Move4& m) {
    Piece4 mover = board.At(fi, fj);
    board.At(fi, fj) = { false, P_NONE, PEON, false, false };

    if (m.kind == MK_ENPASSANT) {
        board.At(epPawn.i, epPawn.j) = { false, P_NONE, PEON, false, false };
    }

    mover.hasMoved = true;
    board.At(m.i, m.j) = mover;   // captures by overwrite

    if (m.kind == MK_CASTLE) {
        // Move the rook to the square the king crossed.
        int ddi = m.i - fi, ddj = m.j - fj;
        bool axisJ = (ddj != 0);
        int dir = axisJ ? (ddj > 0 ? 1 : -1) : (ddi > 0 ? 1 : -1);
        int rc = (dir > 0) ? 10 : 3;
        int ri = axisJ ? fi : rc, rj = axisJ ? rc : fj;
        int rti = axisJ ? fi : (fi + dir), rtj = axisJ ? (fj + dir) : fj;
        Piece4 rook = board.At(ri, rj);
        board.At(ri, rj) = { false, P_NONE, PEON, false, false };
        rook.hasMoved = true;
        board.At(rti, rtj) = rook;
    }

    // En-passant window is open only for the ply right after a double-step.
    if (m.kind == MK_DOUBLE) {
        epValid = true;
        epMid  = { (fi + m.i) / 2, (fj + m.j) / 2 };
        epPawn = { m.i, m.j };
    } else {
        epValid = false;
    }

    if (m.kind == MK_PROMO) {
        promoPending = true;
        promoSquare = { m.i, m.j };
        promoChoice = 0;
        return;   // hold the turn until the player picks a piece
    }

    AdvanceTurn();
}

void FourGame::PromoMove(int delta) {
    if (!promoPending) return;
    promoChoice += delta;
    if (promoChoice < 0) promoChoice = 0;
    if (promoChoice > 3) promoChoice = 3;
}

void FourGame::ConfirmPromo() {
    if (!promoPending) return;
    static const PIECE_TYPE opts[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
    board.At(promoSquare.i, promoSquare.j).type = opts[promoChoice];
    promoPending = false;
    AdvanceTurn();
}

bool FourGame::HasAnyLegalMove(PColor c) const {
    for (int i = 0; i < FourBoard::N; i++)
        for (int j = 0; j < FourBoard::N; j++) {
            const Piece4& p = board.At(i, j);
            if (!p.present || !p.alive || p.color != c) continue;
            std::vector<Move4> mv;
            LegalMoves(i, j, mv);
            if (!mv.empty()) return true;
        }
    return false;
}

void FourGame::EliminatePlayer(int idx, bool checkmate) {
    (void) checkmate;   // M4 uses this for the +20 mate bonus
    players[idx].eliminated = true;
    PColor c = players[idx].color;
    for (int i = 0; i < FourBoard::N; i++)
        for (int j = 0; j < FourBoard::N; j++) {
            Piece4& p = board.At(i, j);
            if (p.present && p.color == c) p.alive = false;   // pieces go grey/dead
        }
}

// Advance to the next player, resolving anyone who has no legal move.
//  - FFA: no-move => eliminated (checkmate or stalemate); game ends with 1 left.
//  - Teams: checkmate => the other team wins; stalemate => draw.
void FourGame::AdvanceTurn() {
    hasSelected = false;
    selMoves.clear();

    for (int guard = 0; guard < 8; guard++) {
        if (mode == FOUR_FFA) {
            int aliveCount = 0, last = -1;
            for (int k = 0; k < 4; k++) if (!players[k].eliminated) { aliveCount++; last = k; }
            if (aliveCount <= 1) {
                gameOver = true;
                resultMsg = std::string(NameFor(players[last].color)) + " gana!";
                return;
            }
        }

        do { current = (current + 1) % 4; } while (players[current].eliminated);

        if (HasAnyLegalMove(CurrentColor())) return;   // normal turn

        bool inCheck = KingAttacked(board, CurrentColor());

        if (mode == FOUR_TEAMS) {
            gameOver = true;
            if (inCheck) {
                int wt = 1 - players[current].team;
                resultMsg = std::string("Equipo ") + (wt == 0 ? "A (Rojo+Amarillo)" : "B (Azul+Verde)") + " gana!";
            } else {
                resultMsg = "Empate (ahogado)";
            }
            return;
        }

        EliminatePlayer(current, inCheck);   // FFA: out, keep going
    }
}

void FourGame::Select() {
    if (gameOver) return;
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

    // Check indicator: ring the current player's king when it's attacked.
    if (active && !gameOver && KingAttacked(board, CurrentColor())) {
        for (int i = 0; i < FourBoard::N; i++)
            for (int j = 0; j < FourBoard::N; j++) {
                const Piece4& p = board.At(i, j);
                if (p.present && p.alive && p.color == CurrentColor() && p.type == KING) {
                    int x, y; CellOrigin(i, j, x, y);
                    Rectangle r = { (float) x, (float) y, (float) CELL, (float) CELL };
                    DrawRectangleLinesEx(r, 3, Color{ 235, 45, 45, 255 });
                }
            }
    }

    // Cursor.
    if (active && !gameOver) {
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
        if (players[pIdx].eliminated) DrawText("Eliminado", s.x + 14, s.y + 118, 18, Color{170,90,90,255});
        else if (isCurrent)           DrawText("Turno",     s.x + 14, s.y + 118, 18, Color{255,205,0,255});
    }

    // Promotion picker (on top of everything).
    if (active && promoPending) {
        static const PIECE_TYPE opts[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
        int cw = CELL + 10;
        int pw = 4 * cw + 16, ph = CELL + 44;
        int px = BOARD_PX / 2 - pw / 2, py = BOARD_PX / 2 - ph / 2;

        DrawRectangle(px, py, pw, ph, Color{ 20, 20, 26, 244 });
        Rectangle border = { (float) px, (float) py, (float) pw, (float) ph };
        DrawRectangleLinesEx(border, 2, Color{ 255, 205, 0, 255 });
        DrawText("Promocion", px + 10, py + 6, 18, WHITE);

        for (int k = 0; k < 4; k++) {
            int cx = px + 8 + k * cw, cy = py + 30;
            if (k == promoChoice) DrawRectangle(cx - 3, cy - 3, CELL + 6, CELL + 6, Color{ 80, 160, 255, 150 });
            std::string key = std::string("w") + TypeChar(opts[k]);
            auto it = textures.find(key);
            if (it != textures.end()) {
                Rectangle src = { 0, 0, (float) it->second.width, (float) it->second.height };
                Rectangle dst = { (float) cx, (float) cy, (float) CELL, (float) CELL };
                DrawTexturePro(it->second, src, dst, (Vector2){ 0, 0 }, 0.0f, TintFor(CurrentColor()));
            }
        }
    }

    // Game-over banner.
    if (gameOver) {
        DrawRectangle(0, 0, BOARD_PX, BOARD_PX, Color{ 0, 0, 0, 150 });
        int fs = 40;
        int w = MeasureText(resultMsg.c_str(), fs);
        DrawText(resultMsg.c_str(), BOARD_PX / 2 - w / 2, BOARD_PX / 2 - fs, fs, WHITE);
        const char* hint = "START: Reiniciar / Salir a menu";
        int hw = MeasureText(hint, 20);
        DrawText(hint, BOARD_PX / 2 - hw / 2, BOARD_PX / 2 + 16, 20, Color{ 210, 210, 210, 255 });
    }
}
