"""Endgame detection: checkmate ends the game and further moves are rejected."""


def play(client, moves):
    for frm, to in moves:
        r = client.move(frm, to)
        assert r == "ok", f"move {frm}->{to} failed: {r}"


def test_fools_mate(classic):
    # 1.f3 e5 2.g4 Qh4#  — the fastest checkmate (Black wins).
    play(classic, [("f2", "f3"), ("e7", "e5"), ("g2", "g4")])
    classic.assert_ok(classic.move("d8", "h4"))
    st = classic.state()
    assert st["state"] == "black_wins", st
    assert st["gameover"] == "1"


def test_scholars_mate(classic):
    # 1.e4 e5 2.Bc4 Nc6 3.Qh5 Nf6?? 4.Qxf7#  — White wins.
    play(classic, [("e2", "e4"), ("e7", "e5"),
                   ("f1", "c4"), ("b8", "c6"),
                   ("d1", "h5"), ("g8", "f6")])
    classic.assert_ok(classic.move("h5", "f7"))
    st = classic.state()
    assert st["state"] == "white_wins", st
    assert st["gameover"] == "1"


def test_moves_rejected_after_game_over(classic):
    play(classic, [("f2", "f3"), ("e7", "e5"), ("g2", "g4"), ("d8", "h4")])
    assert classic.state()["gameover"] == "1"
    assert classic.move("a2", "a3").startswith("err")
