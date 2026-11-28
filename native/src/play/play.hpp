// Akeno Gaming Hub - play mode: Akeno's own title runs one game with an integrated runtime.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hub never hands a game to another application. To play, it restarts its own title
// (sceSystemServiceLoadExec, as Prospero Win and MihawkOS do for their games) with
// "--play=<id>"; main() then skips the hub and gives the screen, the DualSense and the
// audio output to the game's runtime. When the game ends, the title restarts into the hub
// ("--return=<id>"), or goes back to the home screen when a home-screen tile started it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace akeno::play
{

struct Request
{
    std::string game;        // library id to play
    bool from_hub = false;   // return to the hub afterwards (else to the home screen)
    std::string returned;    // the hub was restarted after this game ended
};

// Reads --play=<id>, --play <id>, --from-hub and --return=<id> from the title's arguments.
// The system passes an empty argv[0], so every argument is looked at.
Request parse_arguments(int argc, char **argv);

// The launch payload's hand-over file (<id>\n<unix time>\n): a game for at most a minute.
Request parse_handover(const std::string &text, std::int64_t now);

// The arguments the hub restarts with to play `id`.
std::vector<std::string> play_arguments(const std::string &id, bool from_hub);

// Runs the game in this process. Returns the runtime's exit code, or -1 with `error`.
int run(const std::string &id, std::string &error);

// Whether this build carries the runtime (compiled in).
bool runtime_built(const std::string &runtime);

} // namespace akeno::play
