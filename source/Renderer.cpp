#include <algorithm>
#include "Renderer.h"
#include "Game.h"
#include "compat.h"  // compat::to_string (PS3 newlib lacks std::to_string)

bool Renderer::s_flipped = false;

void Renderer::SetFlipped(bool flipped) {
    s_flipped = flipped;
}

void Renderer::CellOrigin(int i, int j, int& x, int& y) {
    int si = s_flipped ? 7 - i : i;
    int sj = s_flipped ? 7 - j : j;
    x = sj * Game::CELL_SIZE;
    y = si * Game::CELL_SIZE + Game::INFO_BAR_HEIGHT;
}

void Renderer::Clear() {
    ClearBackground(WHITE);
}

void Renderer::RenderBackground() {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            int x, y;
            CellOrigin(i, j, x, y);

            // Colour is tied to the board square (a1 dark), so it stays correct when flipped.
            Color cellColor = GetShadeColor(GetColorOfCell({i, j}));
            DrawRectangle(x, y, Game::CELL_SIZE, Game::CELL_SIZE, cellColor);
        }
    }
}

void Renderer::RenderPieces(const Board& board, const std::map<std::string, Texture>& textures) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            Piece* piece = board.At({i, j});

            if (piece != nullptr) {
                int x, y;
                CellOrigin(i, j, x, y);
                DrawTexture(textures.at(piece->GetName()), x, y, WHITE);
            }
        }
    }
}

void Renderer::RenderMovesSelectedPiece(const std::map<std::string, Texture>& textures, const std::vector<Move>& possibleMoves) {
    for (const Move& move : possibleMoves) {
        int x, y;
        CellOrigin(move.position.i, move.position.j, x, y);
        DrawTexture(textures.at(GetTextureNameFromMoveType(move.type)), x, y, WHITE);
    }
}

void Renderer::RenderGuideText() {
    int padding = 3;
    int characterSize = 10;

    // Rank numbers on the left edge: each board row i is rank (8 - i), drawn at that
    // row's screen position (so when flipped, rank 1 appears at the top).
    int leftBoardCol = s_flipped ? 7 : 0;  // board column under the left screen edge
    for (int i = 0; i < 8; i++) {
        Color textColor = GetShadeColor(Piece::GetInverseColor(GetColorOfCell({i, leftBoardCol})));

        int si = s_flipped ? 7 - i : i;
        int x = padding;
        int y = si * Game::CELL_SIZE + padding + Game::INFO_BAR_HEIGHT;

        char text[2];
        text[0] = 56 - i;   // '8' - i  ->  rank of board row i
        text[1] = 0;

        DrawText(text, x, y, 20, textColor);
    }

    // File letters on the bottom edge: each board column j is file ('a' + j), drawn at
    // that column's screen position (so when flipped, the a-file appears on the right).
    int bottomBoardRow = s_flipped ? 0 : 7;  // board row under the bottom screen edge
    for (int j = 0; j < 8; j++) {
        Color textColor = GetShadeColor(Piece::GetInverseColor(GetColorOfCell({bottomBoardRow, j})));

        int sj = s_flipped ? 7 - j : j;
        int x = (sj + 1) * Game::CELL_SIZE - characterSize - padding;
        int y = Game::WINDOW_HEIGHT - characterSize * 1.75 - padding;

        char text[2];
        text[0] = 97 + j;   // 'a' + j  ->  file of board column j
        text[1] = 0;

        DrawText(text, x, y, 20, textColor);
    }
}

void Renderer::RenderPromotionScreen(const std::map<std::string, Texture>& textures, PIECE_COLOR colorOfPeon) {
    DrawRectangle(0, 0, Game::WINDOW_WIDTH, Game::WINDOW_HEIGHT, Color{0, 0, 0, 127});
    DrawText("Promotion", Game::WINDOW_WIDTH / 2 - 98, Game::WINDOW_HEIGHT / 4, 40, WHITE);

    std::string prefix = colorOfPeon == PIECE_COLOR::C_WHITE ? "w" : "b";

    int textureY = Game::CELL_SIZE * 3 + Game::INFO_BAR_HEIGHT;
    int textY = Game::CELL_SIZE * 4 + 5 + Game::INFO_BAR_HEIGHT;

    // Draw queen.
    {
        DrawTexture(textures.at(prefix + "q"), Game::CELL_SIZE * 2, textureY, WHITE);
        DrawText("Queen", Game::CELL_SIZE * 2 + 9, textY, 20, WHITE);
    }

    // Draw rook.
    {
        DrawTexture(textures.at(prefix + "r"), Game::CELL_SIZE * 3, textureY, WHITE);
        DrawText("Rook", Game::CELL_SIZE * 3 + 14, textY, 20, WHITE);
    }

    // Draw bishop.
    {
        DrawTexture(textures.at(prefix + "b"), Game::CELL_SIZE * 4, textureY, WHITE);
        DrawText("Bishop", Game::CELL_SIZE * 4 + 7, textY, 20, WHITE);
    }

    // Draw knight.
    {
        DrawTexture(textures.at(prefix + "n"), Game::CELL_SIZE * 5, textureY, WHITE);
        DrawText("Knight", Game::CELL_SIZE * 5 + 9, textY, 20, WHITE);
    }
}

static std::string FormatClock(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int) seconds;
    int m = total / 60;
    int s = total % 60;
    std::string ss = compat::to_string(s);
    if (s < 10) ss = "0" + ss;
    return compat::to_string(m) + ":" + ss;
}

void Renderer::RenderInfoBar(int round, double time, bool clockActive, double whiteClock, double blackClock, bool player1IsWhite) {
    DrawRectangle(0, 0, Game::WINDOW_WIDTH, Game::INFO_BAR_HEIGHT, BLACK);

    int padding = 5;
    int y = Game::INFO_BAR_HEIGHT / 2 - 10;

    // Jugador 1 on the left, Jugador 2 on the right, each tagged with its colour
    // (B = Blancas, N = Negras) and, when a clock is active, its remaining time.
    double j1Clock = player1IsWhite ? whiteClock : blackClock;
    double j2Clock = player1IsWhite ? blackClock : whiteClock;

    std::string leftText  = std::string("J1 ") + (player1IsWhite ? "B" : "N");
    std::string rightText = std::string("J2 ") + (player1IsWhite ? "N" : "B");
    if (clockActive) {
        leftText  += " " + FormatClock(j1Clock);
        rightText += " " + FormatClock(j2Clock);
    }

    int rightWidth = MeasureText(rightText.c_str(), 20);
    DrawText(leftText.c_str(), padding, y, 20, WHITE);
    DrawText(rightText.c_str(), Game::WINDOW_WIDTH - rightWidth - padding, y, 20, WHITE);

    // Centre: round number, plus the free-running counter when there's no clock.
    std::string centerText = "R" + compat::to_string(round);
    if (!clockActive) centerText += "  " + compat::to_string((int) time) + "s";
    int cw = MeasureText(centerText.c_str(), 20);
    DrawText(centerText.c_str(), Game::WINDOW_WIDTH / 2 - cw / 2, y, 20, GRAY);
}

void Renderer::RenderMenu(const std::string& title, const std::vector<std::string>& lines, int selected, const std::string& footer) {
    // Dim the whole board behind the panel.
    DrawRectangle(0, 0, Game::WINDOW_WIDTH, Game::WINDOW_HEIGHT, Color{0, 0, 0, 160});

    int rowH = 44;
    int panelW = 440;
    int panelH = 96 + (int) lines.size() * rowH;
    int px = Game::WINDOW_WIDTH / 2 - panelW / 2;
    int py = Game::WINDOW_HEIGHT / 2 - panelH / 2;

    DrawRectangle(px, py, panelW, panelH, Color{28, 28, 34, 244});
    Rectangle border = { (float) px, (float) py, (float) panelW, (float) panelH };
    DrawRectangleLinesEx(border, 2, Color{255, 205, 0, 255});

    DrawText(title.c_str(), px + 20, py + 16, 28, WHITE);

    int y0 = py + 60;
    for (int i = 0; i < (int) lines.size(); i++) {
        int ry = y0 + i * rowH;
        if (i == selected) {
            DrawRectangle(px + 8, ry - 6, panelW - 16, rowH - 4, Color{80, 160, 255, 110});
        }
        DrawText(lines[i].c_str(), px + 24, ry, 22, WHITE);
    }

    const char* hint = footer.empty()
        ? "D-pad mover  Izq/Der cambiar  Cross elegir  O volver"
        : footer.c_str();
    DrawText(hint, px + 16, py + panelH - 26, 13, GRAY);
}

void Renderer::RenderEndScreen(GAME_STATE state) {
    DrawRectangle(0, 0, Game::WINDOW_WIDTH, Game::WINDOW_HEIGHT, Color{0, 0, 0, 127});

    const char* text = "";

    if (state == GAME_STATE::S_WHITE_WINS) {
        text = "White wins";
    } else if (state == GAME_STATE::S_BLACK_WINS) {
        text = "Black wins";
    } else if (state == GAME_STATE::S_STALEMATE) {
        text = "Stalemate";
    }

    int textLength = MeasureText(text, 40);
    DrawText(text, Game::WINDOW_WIDTH / 2 - textLength / 2, Game::WINDOW_HEIGHT / 2, 40, WHITE);
}

void Renderer::RenderCursor(const Position& cursor) {
    int x, y;
    CellOrigin(cursor.i, cursor.j, x, y);

    Rectangle rect = { (float) x, (float) y, (float) Game::CELL_SIZE, (float) Game::CELL_SIZE };
    DrawRectangleLinesEx(rect, 4, Color{255, 205, 0, 255});  // gold outline
}

void Renderer::RenderSelection(const Position& position) {
    int x, y;
    CellOrigin(position.i, position.j, x, y);

    DrawRectangle(x, y, Game::CELL_SIZE, Game::CELL_SIZE, Color{80, 160, 255, 110});  // translucent blue
}

void Renderer::RenderPromotionCursor(int choice) {
    // The promotion options sit on row 3, columns 2..5 (Queen, Rook, Bishop, Knight),
    // matching RenderPromotionScreen.
    int x = (2 + choice) * Game::CELL_SIZE;
    int y = 3 * Game::CELL_SIZE + Game::INFO_BAR_HEIGHT;

    Rectangle rect = { (float) x, (float) y, (float) Game::CELL_SIZE, (float) Game::CELL_SIZE };
    DrawRectangleLinesEx(rect, 4, Color{255, 205, 0, 255});  // gold outline
}

void Renderer::ChangeMouseCursor(const Board& board, const std::vector<Move>& possibleMoves, PIECE_COLOR turn, bool inPromotion) {
    Vector2 mousePosition = GetMousePosition();
    mousePosition.y -= Game::INFO_BAR_HEIGHT;

    Position hoverPosition = {int(mousePosition.y) / Game::CELL_SIZE, int(mousePosition.x) / Game::CELL_SIZE};

    if (!inPromotion) {
        bool isHoveringOverPiece = board.At(hoverPosition) && board.At(hoverPosition)->color == turn;
        auto it = std::find_if(possibleMoves.begin(), possibleMoves.end(), [hoverPosition](const Move& m) {
            return m.position.i == hoverPosition.i && m.position.j == hoverPosition.j;
        });

        bool isHoveringOverMove = it != possibleMoves.end();

        // Set mouse to pointer if hovering over piece or hovering over move.
        if (isHoveringOverPiece || isHoveringOverMove) {
            SetMouseCursor(4);
        } else {
            SetMouseCursor(0);
        }
    } else {
        // If in promotion screen, also set mouse to pointer if hovering over the options.
        if (hoverPosition.i == 3 && hoverPosition.j >= 2 && hoverPosition.j <= 5) {
            SetMouseCursor(4);
        }
    }
}

std::string Renderer::GetTextureNameFromMoveType(MOVE_TYPE moveType) {
    switch (moveType) {
        case MOVE_TYPE::WALK:
        case MOVE_TYPE::DOUBLE_WALK:
        case MOVE_TYPE::ATTACK:
            return "move";

        case MOVE_TYPE::SHORT_CASTLING:
        case MOVE_TYPE::LONG_CASTLING:
            return "castling";

        case MOVE_TYPE::EN_PASSANT:
            return "enpassant";

        case MOVE_TYPE::PROMOTION:
        case MOVE_TYPE::ATTACK_AND_PROMOTION:
            return "promotion";
    }
}

Color Renderer::GetShadeColor(PIECE_COLOR color) {
    return color == PIECE_COLOR::C_WHITE ? Game::LIGHT_SHADE : Game::DARK_SHADE;
}

PIECE_COLOR Renderer::GetColorOfCell(const Position& cellPosition) {
    int startingColorInRow = cellPosition.i % 2 == 0 ? 0 : 1;
    int colorIndex = (startingColorInRow + cellPosition.j) % 2;

    return colorIndex == 0 ? PIECE_COLOR::C_WHITE : PIECE_COLOR::C_BLACK;
}









