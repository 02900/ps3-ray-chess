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


def test_multicheck_double_knight_bonus(ffa):
    # A red knight to (5,7) checks the blue king (3,8) AND the yellow king (6,9) at
    # once. A non-queen double check is worth +5. (Neither king is mated -> game on.)
    ffa.setup4("red",
               "rn@7,8", "rk@13,3",
               "bk@3,8", "yk@6,9", "gk@10,3")
    ffa.assert_ok(ffa.move("7,8", "5,7"))
    assert ffa.points()["red"] == 5, ffa.send("points")


def test_multicheck_double_queen_is_worth_less(ffa):
    # The SAME double check delivered by a queen (rank 5 + file 7) is worth only +1.
    ffa.setup4("red",
               "rq@7,9", "rk@13,3",
               "bk@5,10", "yk@0,7", "gk@10,3")
    ffa.assert_ok(ffa.move("7,9", "5,7"))
    assert ffa.points()["red"] == 1, ffa.send("points")


def test_multicheck_triple_knight_bonus(ffa):
    # A red knight to (5,7) checks blue (3,8), yellow (6,9) AND green (4,5) -> +20.
    ffa.setup4("red",
               "rn@7,8", "rk@13,3",
               "bk@3,8", "yk@6,9", "gk@4,5")
    ffa.assert_ok(ffa.move("7,8", "5,7"))
    assert ffa.points()["red"] == 20, ffa.send("points")
