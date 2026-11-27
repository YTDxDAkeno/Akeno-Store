// Akeno Gaming Hub - live check of every provider against the real stores (host, opt-in).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"
#include "platform/platform.hpp"
#include "install/installer.hpp"
#include "stores/catalog.hpp"

#include <cstdio>
#include <cstring>
#include <thread>

using namespace akeno;

int main(int argc, char **argv)
{
    platform::init();
    net::Client http(net::make_platform_transport());
    ProviderContext context;
    context.http = &http;
    context.cache_dir = platform::cache_dir();
    context.state_dir = platform::data_dir();
    fs::make_dirs(context.cache_dir);
    std::vector<std::unique_ptr<Provider>> providers;
    providers.push_back(make_prosperostore_provider());
    providers.push_back(make_recompstore_provider());
    providers.push_back(make_itch_provider());
    providers.push_back(make_gamejolt_provider());
    providers.push_back(make_gog_provider());
    providers.push_back(make_epic_provider());
    Catalog catalog(context, std::move(providers));
    catalog.refresh();
    while (catalog.refreshing())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int failed = 0;
    for (const ProviderStatus &status : catalog.status())
    {
        std::printf("%-14s %-8s %4zu entries %s\n", status.id.c_str(),
                    status.state == ProviderStatus::State::Ready     ? "ready"
                    : status.state == ProviderStatus::State::Offline ? "offline"
                                                                     : "FAILED",
                    status.count, status.message.c_str());
        failed += status.state == ProviderStatus::State::Failed;
    }
    const auto games = catalog.games();
    std::printf("merged: %zu entries\n", games.size());
    int install = 0, runtime = 0, external = 0;
    for (const Game &game : games)
    {
        install += game.access == Access::Install;
        runtime += game.access == Access::Runtime;
        external += game.access == Access::External;
    }
    std::printf("installable %d, runtime %d, store page %d\n", install, runtime, external);
    // "install:<TITLEID>": the full download, verification and install into the host's
    // simulated /data/homebrew, through the same Installer the app uses.
    for (int i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], "install:", 8) == 0)
        {
            Library library(fs::join(platform::data_dir(), "receipts"));
            Installer installer(http, library, fs::join(platform::data_dir(), "receipts"),
                                [&](Game &game, std::string &error) { return catalog.resolve(game, error); });
            for (const Game &game : games)
                if (game.title_id == argv[i] + 8)
                {
                    const int id = installer.install(game, platform::install_roots().front().path);
                    for (;;)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        bool finished = false;
                        for (const Job &job : installer.jobs())
                            if (job.id == id && job.finished())
                            {
                                finished = true;
                                std::printf("install %s: %s %s (%llu bytes)\n", game.title_id.c_str(),
                                            job_state_label(job.state), job.error.c_str(),
                                            static_cast<unsigned long long>(job.done));
                                failed += job.state != JobState::Done;
                            }
                        if (finished)
                            break;
                    }
                }
            for (const InstalledTitle &title : library.scan({platform::install_roots().front().path}))
                std::printf("installed: %s %s %s managed=%d %s\n", title.title_id.c_str(), title.name.c_str(),
                            title.content_version.c_str(), title.managed ? 1 : 0, title.folder.c_str());
        }
    // Resolve the requested titles through their providers (download data only, no download).
    for (int i = 1; i < argc; ++i)
        for (Game game : games)
            if (game.title_id == argv[i] || game.id == argv[i])
            {
                std::string error;
                const bool ok = catalog.resolve(game, error);
                std::printf("resolve %s (%s): %s %s sha256=%s size=%llu %s\n", game.id.c_str(), game.title.c_str(),
                            ok ? "ok" : "FAILED", game.artifact_url.c_str(), game.sha256.c_str(),
                            static_cast<unsigned long long>(game.size), error.c_str());
                failed += !ok;
                break;
            }
    return failed ? 1 : 0;
}
