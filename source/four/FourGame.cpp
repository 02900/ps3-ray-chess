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
