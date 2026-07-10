"""4PC points & elimination, built from set-up positions (`setup4`).

Points (chess.com 4PC): P=1, N=3, B=5, R=5, Q=9, K=20; checkmating a player = +20
to the mater; being stalemated = +20 to the stalemated player; the mated/stalemated
player is eliminated and its pieces turn grey ("dead"). All positions keep a king for
every colour so nobody is eliminated by accident.
"""


def test_capture_awards_piece_value(ffa):
    # Red rook takes an adjacent blue pawn (value 1).
    ffa.setup4("red",
               "rr@5,5", "rk@13,7",
               "bp@5,6", "bk@0,7",
               "yk@6,0", "gk@6,13")
    assert ffa.at("5,6") == "blue peon"
    ffa.assert_ok(ffa.move("5,5", "5,6"))
    assert ffa.points()["red"] == 1
    assert ffa.at("5,6") == "red rook"


def test_capture_awards_queen_value(ffa):
    # Red rook takes an adjacent blue queen (value 9).
    ffa.setup4("red",
               "rr@5,5", "rk@13,7",
               "bq@5,6", "bk@0,7",
               "yk@6,0", "gk@6,13")
    ffa.assert_ok(ffa.move("5,5", "5,6"))
    assert ffa.points()["red"] == 9


def test_checkmate_eliminates_and_scores(ffa):
    # Red queen on (1,7), defended down file 7 by a rook, mates the lone blue king on
    # (0,7). A quiet red move advances the turn to blue -> checkmate detected.
    ffa.setup4("red",
               "rq@1,7", "rr@13,7", "rk@13,10", "rp@10,10",
               "bk@0,7",
               "yk@6,0", "gk@6,13")
    assert ffa.legal("0,7") == set()           # the blue king is already trapped
    ffa.assert_ok(ffa.move("10,10", "9,10"))   # quiet red move -> blue's turn -> mate
    pts = ffa.points()
    assert pts["red"] == 20, pts               # +20 for the checkmate
    assert "dead" in ffa.at("0,7")             # blue king greyed out
    assert "(elim)" in ffa.send("points")      # blue marked eliminated


def test_stalemate_eliminates_and_scores_stalemated(ffa):
    # Blue king on (0,3) has no legal move and is NOT in check: two red rooks cover
    # every escape ((13,4) covers file 4 -> (0,4),(1,4); (1,10) covers rank 1 ->
    # (1,3),(1,4)) without either attacking (0,3). (1,10) is playable; (1,13) is a
    # cut-out corner.
    ffa.setup4("red",
               "rr@13,4", "rr@1,10", "rk@13,10", "rp@10,10",
               "bk@0,3",
               "yk@6,0", "gk@6,13")
    assert ffa.legal("0,3") == set()           # trapped
    ffa.assert_ok(ffa.move("10,10", "9,10"))   # quiet red move -> blue's turn -> stalemate
    pts = ffa.points()
    assert pts["blue"] == 20, pts              # the stalemated player scores +20
    assert "dead" in ffa.at("0,3")
    assert "(elim)" in ffa.send("points")
    # The game is not over: red / yellow / green are still in.
    assert ffa.state()["gameover"] == "0"
