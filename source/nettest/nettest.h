#ifndef RAY_CHESS_NETTEST_H
#define RAY_CHESS_NETTEST_H

// TCP command server for the end-to-end test harness. A PC connects over TCP and
// sends line-based ASCII commands (one per line); the game replies with one line.
// The socket runs on a background thread, but it NEVER touches game state: it hands
// each command to the main loop (PopCommand) and blocks until the main loop posts a
// reply (PostReply). Everything here is compiled only when built with -DNETTEST, so
// release builds open no port and add no networking.
#ifdef NETTEST

#include <string>

namespace nettest {

// Load the net module, bind/listen on the command port, spawn the server thread.
// Call once at startup. Safe no-op if networking fails to come up.
void Start();

// --- Main-thread handshake (call from the render loop) ----------------------
// If a command is waiting, move it into `cmd` and return true; the caller MUST
// then produce a reply and call PostReply exactly once. Only one command is ever
// outstanding at a time (the client waits for each reply before sending again).
bool PopCommand(std::string& cmd);
void PostReply(const std::string& reply);

// --- Synthetic pad (all main-thread) ----------------------------------------
// A `press` command sets a button for the current frame; the input helpers OR it
// in, so it drives menus / cursor / moves exactly like a real controller. Buttons
// are raylib GAMEPAD_BUTTON_* ints. SynthClear() is called at the end of each frame
// so a press lasts exactly one (edge-triggered) frame.
void SynthPress(int button);
bool SynthPressed(int button);
bool SynthDown(int button);
void SynthClear();

}  // namespace nettest

#endif  // NETTEST
#endif  // RAY_CHESS_NETTEST_H
