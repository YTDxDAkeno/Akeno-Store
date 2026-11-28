// Akeno Gaming Hub - EasyRPG Player as Akeno's integrated RPG Maker 2000/2003 runtime.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built with EasyRPG's own flags (C++17, exceptions, RTTI) into libakeno-easyrpg.a by
// native/tools/easyrpg.sh, so Akeno's core keeps its flags. The player opens its SDL2
// window full screen, reads the DualSense through SDL's game controller API and plays
// WAV and MIDI through SDL audio, all inside Akeno's own title.
#include "output.h"
#include "player.h"

#include <exception>
#include <string>
#include <vector>

extern "C" int akeno_easyrpg_run(const char *game, const char *saves, const char *config, const char *rtp,
                                 const char *encoding, int fullscreen)
{
    std::vector<std::string> args = {"easyrpg-player", "--project-path", game,   "--save-path",
                                     saves,            "--config-path",  config, fullscreen ? "--fullscreen" : "--window",
                                     "--no-log-color"};
    if (rtp && *rtp)
        args.insert(args.end(), {"--rtp-path", rtp});
    if (encoding && *encoding)
        args.insert(args.end(), {"--encoding", encoding});
    try
    {
        Player::Init(std::move(args));
        Player::Run();
    }
    catch (const std::exception &error)
    {
        Output::Warning("Akeno: the player stopped: {}", error.what());
        return -2;
    }
    return Player::exit_code;
}
