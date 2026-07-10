"""UI / menu navigation driven by synthetic controller presses (not high-level
commands) — exercises the real input path through the screens."""


def test_pause_menu_opens(classic):
    assert classic.state()["screen"] == "game"
    classic.press("start")
    assert classic.state()["screen"] == "pause"


def test_pause_resume(classic):
    classic.press("start")
    assert classic.state()["screen"] == "pause"
    classic.press("cross")            # index 0 = "Reanudar juego en curso"
    assert classic.state()["screen"] == "game"


def test_navigate_pause_to_mode_select(classic):
    classic.press("start")            # game -> pause
    for _ in range(6):                # move to "Salir a menu principal" (last item)
        classic.press("down")
    classic.press("cross")            # -> main menu
    assert classic.state()["screen"] == "main"

    classic.press("cross")            # index 0 "Nueva Partida" -> mode select
    assert classic.state()["screen"] == "modesel"

    classic.press("circle")           # back out to main
    assert classic.state()["screen"] == "main"


def test_cursor_moves_in_game(classic):
    # Driving the D-pad should not crash and the game stays running; a subsequent
    # move still works (the synthetic pad only nudges the cursor here).
    for _ in range(3):
        classic.press("up")
        classic.press("right")
    assert classic.state()["screen"] == "game"
    classic.assert_ok(classic.move("e2", "e4"))
