"""4PC Teams (2v2): Red+Yellow (team A) vs Blue+Green (team B).
Checkmating any one enemy wins for the mating team; a stalemate is a draw."""


def test_teams_checkmate_wins_for_team(teams):
    # Red mates the lone blue king -> team A (Red+Yellow) wins immediately.
    teams.setup4("red",
                 "rq@1,7", "rr@13,7", "rk@13,10", "rp@10,10",
                 "bk@0,7",
                 "yk@6,0", "gk@6,13")
    assert teams.legal("0,7") == set()
    teams.assert_ok(teams.move("10,10", "9,10"))
    assert teams.state()["gameover"] == "1"
    assert "Equipo A" in teams.send("result"), teams.send("result")


def test_teams_stalemate_is_a_draw(teams):
    # Blue is stalemated (no move, not in check) -> draw, not a win.
    teams.setup4("red",
                 "rr@13,4", "rr@1,10", "rk@13,10", "rp@10,10",
                 "bk@0,3",
                 "yk@6,0", "gk@6,13")
    assert teams.legal("0,3") == set()
    teams.assert_ok(teams.move("10,10", "9,10"))
    assert teams.state()["gameover"] == "1"
    assert "mpate" in teams.send("result").lower(), teams.send("result")
