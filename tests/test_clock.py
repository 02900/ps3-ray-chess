"""Fischer clock: running out of time loses (flag-fall). `setclock` forces the clock
active so we don't have to wait out a real time control."""


def test_white_flag_fall_loses(classic):
    # Bare kings, white to move; white's clock is ~0 -> white flags -> Black wins.
    classic.setup("4k3/8/8/8/8/8/8/4K3", "w")
    assert classic.send("setclock 0.001 60") == "ok"
    classic.ping(); classic.ping()          # let a couple of frames tick the clock
    st = classic.state()
    assert st["state"] == "black_wins", st
    assert st["gameover"] == "1"


def test_black_flag_fall_loses(classic):
    classic.setup("4k3/8/8/8/8/8/8/4K3", "b")
    assert classic.send("setclock 60 0.001") == "ok"
    classic.ping(); classic.ping()
    st = classic.state()
    assert st["state"] == "white_wins", st
    assert st["gameover"] == "1"


def test_clock_does_not_flag_the_side_not_to_move(classic):
    # White to move with a healthy clock; black's low clock must NOT fall (only the
    # side to move is ticking).
    classic.setup("4k3/8/8/8/8/8/8/4K3", "w")
    assert classic.send("setclock 60 0.001") == "ok"
    classic.ping(); classic.ping()
    assert classic.state()["gameover"] == "0"
