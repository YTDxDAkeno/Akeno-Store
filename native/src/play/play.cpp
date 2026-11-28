// Akeno Gaming Hub - play mode dispatch.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "play/play.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"
#include "play/library.hpp"

#include <cstring>

namespace akeno::play
{

// Implemented in easyrpg.cpp when the EasyRPG runtime is compiled in.
int run_easyrpg(const GameProfile &profile, std::string &error);

bool runtime_built(const std::string &runtime)
{
#ifdef AKENO_WITH_EASYRPG
    if (runtime == "easyrpg")
        return true;
#endif
    (void)runtime;
    return false;
}

int run(const std::string &id, std::string &error)
{
    auto profile = read_profile(id);
    if (!profile)
    {
        error = "Kein Startprofil für " + id;
        return -1;
    }
    if (!fs::is_dir(profile->folder))
    {
        error = "Die Spieldateien fehlen: " + profile->folder;
        return -1;
    }
    fs::make_dirs(profile->saves);
    profile->last_played = platform::unix_time();
    write_profile(*profile);
    platform::log("[play] %s (%s) with runtime %s from %s", profile->title.c_str(), profile->id.c_str(),
                  profile->runtime.c_str(), profile->folder.c_str());
#ifdef AKENO_WITH_EASYRPG
    if (profile->runtime == "easyrpg")
        return run_easyrpg(*profile, error);
#endif
    error = "Die Akeno-Runtime \"" + profile->runtime + "\" ist in diesem Build nicht enthalten";
    return -1;
}

} // namespace akeno::play
