"""Classic 2-player rules: starting position, legal move sets, captures, turn order."""

from conftest import START_FEN


def test_start_position(classic):
    st = classic.state()
    assert st["mode"] == "classic"
    assert st["screen"] == "game"
    assert st["turn"] == "white"
    assert st["round"] == "1"
    assert st["gameover"] == "0"
    assert classic.board() == START_FEN


def test_pawn_legal_moves(classic):
    # A starting pawn may step one or two squares.
    assert classic.legal("e2") == {"e3", "e4"}
    # Black's pawns are not to move on white's turn.
    assert classic.legal("e7") == set()


def test_knight_legal_moves(classic):
    assert classic.legal("g1") == {"f3", "h3"}
    assert classic.legal("b1") == {"a3", "c3"}


def test_blocked_piece_has_no_moves(classic):
    # The bishop on c1 is hemmed in at the start.
    assert classic.legal("c1") == set()
    assert classic.legal("d1") == set()  # queen too


def test_make_a_move_updates_board_and_turn(classic):
    classic.assert_ok(classic.move("e2", "e4"))
    st = classic.state()
    assert st["turn"] == "black"
    assert classic.at("e4") == "white peon"
    assert classic.at("e2") == "empty"
    assert classic.board().startswith("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP")


def test_turn_alternation_and_round(classic):
    classic.move("e2", "e4")
    assert classic.state()["turn"] == "black"
    classic.move("e7", "e5")
    st = classic.state()
    assert st["turn"] == "white"
    assert st["round"] == "2"  # round advances when it returns to white


def test_illegal_move_rejected(classic):
    # A pawn cannot jump three squares, and the board must be unchanged.
    assert classic.move("e2", "e5").startswith("err")
    assert classic.board() == START_FEN
    assert classic.state()["turn"] == "white"


def test_moving_wrong_color_rejected(classic):
    # It's white's turn; black may not move.
    assert classic.move("e7", "e5").startswith("err")
    assert classic.board() == START_FEN


def test_capture(classic):
    classic.move("e2", "e4")
    classic.move("d7", "d5")
    # White pawn takes the black d5 pawn.
    classic.assert_ok(classic.move("e4", "d5"))
    assert classic.at("d5") == "white peon"
    assert classic.at("e4") == "empty"


def test_check_detection(classic):
    # Scholar's-mate setup delivers check on move 7 (Qxf7).
    for frm, to in [("e2", "e4"), ("e7", "e5"),
                    ("f1", "c4"), ("b8", "c6"),
                    ("d1", "h5"), ("g8", "f6")]:
        classic.assert_ok(classic.move(frm, to))
    # Qh5xf7 is checkmate (Scholar's mate) — see test_checkmate; here just confirm
    # the mechanism: after Bc4/Qh5, f7 is attacked.
    assert "f7" in classic.legal("h5")


def test_history_tracks_san(classic):
    classic.move("e2", "e4")
    classic.move("e7", "e5")
    classic.move("g1", "f3")
    hist = classic.history()
    assert hist[:3] == ["e4", "e5", "Nf3"]
