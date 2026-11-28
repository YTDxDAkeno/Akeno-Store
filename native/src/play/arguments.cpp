// Akeno Gaming Hub - play mode arguments (shared by the hub, the play mode and the tests).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util.hpp"

#include <cstdlib>
#include "play/library.hpp"
#include "play/play.hpp"

namespace akeno::play
{

Request parse_arguments(int argc, char **argv)
{
    Request request;
    for (int i = 0; i < argc; ++i)
    {
        if (!argv[i])
            continue;
        const std::string arg = argv[i];
        if (starts_with(arg, "--play="))
            request.game = arg.substr(7);
        else if (arg == "--play" && i + 1 < argc && argv[i + 1])
            request.game = argv[++i];
        else if (arg == "--from-hub")
            request.from_hub = true;
        else if (starts_with(arg, "--return="))
            request.returned = arg.substr(9);
    }
    if (!valid_library_id(request.game))
        request.game.clear();
    if (!valid_library_id(request.returned))
        request.returned.clear();
    return request;
}

Request parse_handover(const std::string &text, std::int64_t now)
{
    Request request;
    const auto lines = split(text, '\n');
    if (lines.size() < 2)
        return request;
    const std::string id = trim(lines[0]);
    const std::int64_t when = std::strtoll(lines[1].c_str(), nullptr, 10);
    if (valid_library_id(id) && when > 0 && now - when >= 0 && now - when <= 60)
        request.game = id;
    return request;
}

std::vector<std::string> play_arguments(const std::string &id, bool from_hub)
{
    std::vector<std::string> args{"--play=" + id};
    if (from_hub)
        args.push_back("--from-hub");
    return args;
}

} // namespace akeno::play
