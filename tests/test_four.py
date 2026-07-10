"""4-player chess (chess.com 4PC): board setup, turn order, points, wrong-turn guard.

Coordinates are numeric i,j on the 14x14 grid (i = row 0..13 top->bottom, j = col).
Red starts at the bottom (back rank row 13, pawns row 12, advancing toward row 0).
"""


def test_ffa_start(ffa):
    st = ffa.state()
    assert st["mode"] == "ffa"
    assert st["screen"] == "game"
    assert st["turn"] == "red"
    assert st["gameover"] == "0"
    # 14 rows of 14 two-char cells (joined by '/').
    rows = ffa.board().split("/")
    assert len(rows) == 14
    assert all(len(r) == 28 for r in rows), [len(r) for r in rows]
    # Everyone starts at zero points.
    assert ffa.points() == {"red": 0, "blue": 0, "yellow": 0, "green": 0}


def test_ffa_starting_pieces(ffa):
    # Red pawns line row 12 (cols 3..10); the back rank is row 13.
    assert ffa.at("12,5") == "red peon"
    assert ffa.at("13,7") == "red king"     # K at index 7 on the back rank
    # A removed corner reports "off".
    assert ffa.at("0,0") == "off"


def test_ffa_pawn_legal_moves(ffa):
    # A red pawn may step one or two squares toward row 0.
    assert ffa.legal("12,5") == {"11,5", "10,5"}


def test_ffa_turn_order(ffa):
    # Red -> Blue -> Yellow -> Green.
    ffa.assert_ok(ffa.move("12,5", "11,5"))
    assert ffa.state()["turn"] == "blue"


def test_ffa_wrong_turn_rejected(ffa):
    ffa.assert_ok(ffa.move("12,5", "11,5"))   # red moves; now blue's turn
    assert ffa.move("12,6", "11,6").startswith("err")   # red can't move again


def test_teams_start(teams):
    st = teams.state()
    assert st["mode"] == "teams"
    assert st["turn"] == "red"
    assert st["gameover"] == "0"
