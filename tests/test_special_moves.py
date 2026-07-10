"""Special moves reached via real (short) opening sequences: castling, en passant,
promotion. No position-setup command is needed for these."""


def play(client, moves):
    for frm, to in moves:
        r = client.move(frm, to)
        assert r == "ok", f"move {frm}->{to} failed: {r}"


def test_short_castling(classic):
    # 1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 clears white's king-side.
    play(classic, [("e2", "e4"), ("e7", "e5"),
                   ("g1", "f3"), ("b8", "c6"),
                   ("f1", "c4"), ("f8", "c5")])
    # White king may now castle short (e1 -> g1).
    assert "g1" in classic.legal("e1")
    classic.assert_ok(classic.move("e1", "g1"))
    assert classic.at("g1") == "white king"
    assert classic.at("f1") == "white rook"   # rook jumped over
    assert classic.at("e1") == "empty"
    assert classic.at("h1") == "empty"


def test_en_passant(classic):
    # Manoeuvre a white pawn to a5, then black plays b7-b5; a5xb6 e.p.
    play(classic, [("a2", "a4"), ("h7", "h6"),
                   ("a4", "a5"), ("b7", "b5")])
    # The en-passant capture a5 -> b6 must be offered.
    assert "b6" in classic.legal("a5")
    classic.assert_ok(classic.move("a5", "b6"))
    assert classic.at("b6") == "white peon"
    assert classic.at("b5") == "empty"   # the captured pawn is removed


def test_promotion(classic):
    # Zig-zag the g/h pawn up by captures to the 8th rank, capturing the h8 rook to
    # promote: 1.g4 h5 2.gxh5 a6 3.h6 a5 4.hxg7 a4 5.gxh8=Q
    # (a8 is blocked by the black rook, so promotion must come via a capture.)
    play(classic, [("g2", "g4"), ("h7", "h5"),
                   ("g4", "h5"), ("a7", "a6"),
                   ("h5", "h6"), ("a6", "a5"),
                   ("h6", "g7"), ("a5", "a4")])
    # g7 x h8 reaches the last rank and triggers the promotion picker.
    classic.assert_ok(classic.move("g7", "h8"))
    assert classic.state()["state"] == "promotion"
    classic.assert_ok(classic.promote("q"))
    st = classic.state()
    assert st["state"] == "running"
    assert st["turn"] == "black"          # promotion completed, turn passed
    assert classic.at("h8") == "white queen"
