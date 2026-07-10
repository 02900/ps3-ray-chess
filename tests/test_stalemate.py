"""Classic stalemate and setup-driven end states (via the `setup` FEN command)."""


def test_classic_stalemate(classic):
    # Textbook K+Q vs K stalemate, black to move: the h8 king has no legal move and
    # is NOT in check.
    assert classic.setup("7k/5Q2/6K1/8/8/8/8/8", "b") == "ok"
    st = classic.state()
    assert st["state"] == "stalemate", st
    assert st["gameover"] == "1"
    assert st["check"] == "0"
    assert classic.legal("h8") == set()   # the king is boxed in


def test_setup_check_not_mate(classic):
    # A rook checks the black king down the e-file, but the king can step aside:
    # running + in check, not game over.
    assert classic.setup("4k3/8/8/8/8/8/8/4R2K", "b") == "ok"
    st = classic.state()
    assert st["state"] == "running", st
    assert st["check"] == "1"
    assert st["gameover"] == "0"
    assert classic.legal("e8")             # escape squares exist


def test_setup_roundtrip(classic):
    # setup restores an arbitrary position and the board reads it back exactly.
    fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R"
    assert classic.setup(fen, "w") == "ok"
    assert classic.board() == fen
    assert classic.turn() == "white"
