#ifndef RAY_CHESS_RENDERER_H
#define RAY_CHESS_RENDERER_H

#include "raylib.h"
#include "pieces/Piece.h"
#include "Game.h"

class Renderer {
public:
    static void Clear();
    static void RenderBackground();
    static void RenderPieces(const Board& board, const std::map<std::string, Texture>& textures);
    static void RenderMovesSelectedPiece(const std::map<std::string, Texture>& textures, const std::vector<Move>& possibleMoves);
    static void RenderGuideText();
    static void RenderPromotionScreen(const std::map<std::string, Texture>& textures, PIECE_COLOR colorOfPeonBeingPromoted);
    static void RenderInfoBar(int round, double time, bool clockActive, double whiteClock, double blackClock, bool player1IsWhite);
    static void RenderEndScreen(GAME_STATE state);
    static void RenderMenu(const std::string& title, const std::vector<std::string>& lines, int selected, const std::string& footer);
    static void ChangeMouseCursor(const Board& board, const std::vector<Move>& possibleMoves, PIECE_COLOR turn, bool inPromotion);

    // PS3 gamepad affordances (replace the desktop mouse cursor).
    static void RenderCursor(const Position& cursor);       // outline the hovered cell
    static void RenderSelection(const Position& position);  // highlight the selected piece's cell
    static void RenderPromotionCursor(int choice);          // outline the chosen promotion option

    // Board orientation. When flipped, black sits at the bottom (view rotated 180°);
    // pieces stay upright — only cell positions and coordinate labels are remapped.
    static void SetFlipped(bool flipped);

private:
    static std::string GetTextureNameFromMoveType(MOVE_TYPE moveType);
    static Color GetShadeColor(PIECE_COLOR color);
    static PIECE_COLOR GetColorOfCell(const Position& cellPosition);

    // Screen-space top-left pixel of board cell (i,j), honoring the flip.
    static void CellOrigin(int i, int j, int& x, int& y);
    static bool s_flipped;
};

#endif //RAY_CHESS_RENDERER_H
