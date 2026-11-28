// Akeno Gaming Hub - download manager and title installer.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "install/library.hpp"
#include "net/http.hpp"
#include "stores/game.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace akeno
{

enum class JobKind
{
    Install,
    Uninstall
};

enum class JobState
{
    Queued,
    Resolving,
    Downloading,
    Verifying,
    Unpacking,
    Finishing,
    Done,
    Failed,
    Cancelled
};

const char *job_state_label(JobState state);

struct Job
{
    int id = 0;
    JobKind kind = JobKind::Install;
    JobState state = JobState::Queued;
    std::string game_id;
    std::string title;
    std::string title_id;
    std::string root;     // install root (install) or the title folder (uninstall)
    bool library = false; // a game for Akeno's own library (played in Akeno's play mode)
    std::string profile;  // its library id
    std::uint64_t done = 0, total = 0;
    double bytes_per_second = 0;
    std::string error;
    std::string note; // e.g. "Update ersetzt 01.000.010"
    bool finished() const
    {
        return state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled;
    }
};

class Installer
{
  public:
    // Completes a catalog entry's download data through its provider.
    using Resolve = std::function<bool(Game &, std::string &)>;

    Installer(net::Client &http, const Library &library, std::string receipts_dir, Resolve resolve);
    ~Installer();
    Installer(const Installer &) = delete;
    Installer &operator=(const Installer &) = delete;

    int install(const Game &game, const std::string &root);
    int uninstall(const InstalledTitle &title);
    // Removes a game from Akeno's library (downloaded files; saves are kept).
    int uninstall_library(const std::string &id, const std::string &title);
    void cancel(int job);
    std::vector<Job> jobs() const;
    bool busy() const;
    // Increments whenever installed titles changed; the UI rescans the library then.
    std::uint64_t generation() const { return generation_.load(); }
    void clear_finished();

    // Work area on the same volume as `root` (renames stay atomic): <parent>/akeno-work.
    static std::string work_dir_for(const std::string &root);

  private:
    struct Entry
    {
        Job job;
        Game game;
        std::shared_ptr<net::Cancel> cancel;
    };

    void run();
    void process(Entry &entry);
    bool do_install(Entry &entry, std::string &error);
    bool do_uninstall(Entry &entry, std::string &error);
    bool finish_library(Entry &entry, const std::string &archive, const std::string &job_dir, const std::string &digest,
                        std::string &error);
    void update(int id, const std::function<void(Job &)> &change);

    net::Client &http_;
    const Library &library_;
    std::string receipts_;
    Resolve resolve_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Entry>> entries_;
    std::atomic<std::uint64_t> generation_{0};
    int next_id_ = 1;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace akeno
