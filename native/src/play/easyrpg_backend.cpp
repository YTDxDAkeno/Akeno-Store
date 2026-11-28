// Akeno Gaming Hub - play mode side of the integrated EasyRPG runtime.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef AKENO_WITH_EASYRPG
#include "core/fs.hpp"
#include "platform/platform.hpp"
#include "play/library.hpp"

#include <SDL.h>

extern "C" int akeno_easyrpg_run(const char *game, const char *saves, const char *config, const char *rtp,
                                 const char *encoding, int fullscreen);

namespace akeno::play
{

int run_easyrpg(const GameProfile &profile, std::string &error)
{
    const std::string config = fs::join(library_dir(profile.id), "config");
    fs::make_dirs(config);
    // RTP data, when the player supplies it (copied by the user): <data>/rtp/2000 and /2003.
    const std::string rtp = fs::join(platform::data_dir(), "rtp");
    if (platform::is_console())
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"); // the PS5 SDL has only the software renderer
    const int code = akeno_easyrpg_run(profile.folder.c_str(), profile.saves.c_str(), config.c_str(),
                                       fs::is_dir(rtp) ? rtp.c_str() : "", "", platform::is_console() ? 1 : 0);
    platform::log("[play] EasyRPG ended with %d", code);
    if (code < 0)
        error = "Der RPG-Maker-Player wurde mit einem Fehler beendet (Protokoll: " + config + ")";
    return code;
}

} // namespace akeno::play
#endif
