// Akeno Gaming Hub - download manager and title installer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One job at a time on a worker thread. An install is: resolve the provider's download,
// stream it to the work area while hashing, compare with the expected SHA-256, locate
// exactly one title folder in the ZIP, check its title ID, unpack beside the destination,
// then swap it in with a rename (the previous version is kept in akeno-work/previous).
// Nothing in the destination changes until the final rename.
#include "install/installer.hpp"

#include "core/fs.hpp"
#include "core/hash.hpp"
#include "core/util.hpp"
#include "core/json.hpp"
#include "core/zip.hpp"
#include "platform/platform.hpp"
#include "play/components.hpp"
#include "play/library.hpp"
#include "runtime/runtimes.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

namespace akeno
{

const char *job_state_label(JobState state)
{
    switch (state)
    {
    case JobState::Queued:
        return "Wartet";
    case JobState::Resolving:
        return "Wird vorbereitet";
    case JobState::Downloading:
        return "Download";
    case JobState::Verifying:
        return "Wird geprüft";
    case JobState::Unpacking:
        return "Wird entpackt";
    case JobState::Finishing:
        return "Wird installiert";
    case JobState::Done:
        return "Fertig";
    case JobState::Failed:
        return "Fehlgeschlagen";
    case JobState::Cancelled:
        return "Abgebrochen";
    }
    return "";
}

Installer::Installer(net::Client &http, const Library &library, std::string receipts_dir, Resolve resolve)
    : http_(http), library_(library), receipts_(std::move(receipts_dir)), resolve_(std::move(resolve))
{
    worker_ = std::thread([this] { run(); });
}

Installer::~Installer()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto &entry : entries_)
            entry->cancel->requested = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

std::string Installer::work_dir_for(const std::string &root) { return fs::join(fs::parent(root), "akeno-work"); }

int Installer::install(const Game &game, const std::string &root)
{
    auto entry = std::make_shared<Entry>();
    entry->game = game;
    entry->cancel = std::make_shared<net::Cancel>();
    std::lock_guard lock(mutex_);
    for (const auto &existing : entries_)
        if (!existing->job.finished() && existing->job.game_id == game.id)
            return existing->job.id;
    entry->job.id = next_id_++;
    entry->job.kind = JobKind::Install;
    entry->job.game_id = game.id;
    entry->job.title = game.title;
    entry->job.title_id = game.title_id;
    entry->job.root = root;
    entry->job.total = game.size;
    entry->job.library = game.runtime == "akeno";
    entry->job.profile = game.profile;
    entries_.push_back(entry);
    wake_.notify_all();
    return entry->job.id;
}

int Installer::uninstall(const InstalledTitle &title)
{
    auto entry = std::make_shared<Entry>();
    entry->cancel = std::make_shared<net::Cancel>();
    std::lock_guard lock(mutex_);
    entry->job.id = next_id_++;
    entry->job.kind = JobKind::Uninstall;
    entry->job.title = title.name;
    entry->job.title_id = title.title_id;
    entry->job.root = title.folder;
    entry->job.game_id = title.source_game_id;
    entries_.push_back(entry);
    wake_.notify_all();
    return entry->job.id;
}

int Installer::install_component(const std::string &component, const Game &source)
{
    auto entry = std::make_shared<Entry>();
    entry->game = source;
    entry->cancel = std::make_shared<net::Cancel>();
    std::lock_guard lock(mutex_);
    for (const auto &existing : entries_)
        if (!existing->job.finished() && existing->job.component == component)
            return existing->job.id;
    const ComponentSpec *spec = component_spec(component);
    entry->job.id = next_id_++;
    entry->job.kind = JobKind::Install;
    entry->job.component = component;
    entry->job.game_id = source.id;
    entry->job.title = spec ? spec->name : component;
    entry->job.title_id = source.title_id;
    entry->job.root = components_root();
    entry->job.total = source.size;
    entries_.push_back(entry);
    wake_.notify_all();
    return entry->job.id;
}

int Installer::uninstall_library(const std::string &id, const std::string &title)
{
    auto entry = std::make_shared<Entry>();
    entry->cancel = std::make_shared<net::Cancel>();
    std::lock_guard lock(mutex_);
    entry->job.id = next_id_++;
    entry->job.kind = JobKind::Uninstall;
    entry->job.title = title;
    entry->job.library = true;
    entry->job.profile = id;
    entries_.push_back(entry);
    wake_.notify_all();
    return entry->job.id;
}

void Installer::cancel(int id)
{
    std::lock_guard lock(mutex_);
    for (auto &entry : entries_)
        if (entry->job.id == id && !entry->job.finished())
        {
            entry->cancel->requested = true;
            if (entry->job.state == JobState::Queued)
                entry->job.state = JobState::Cancelled;
        }
}

std::vector<Job> Installer::jobs() const
{
    std::lock_guard lock(mutex_);
    std::vector<Job> out;
    for (const auto &entry : entries_)
        out.push_back(entry->job);
    return out;
}

bool Installer::busy() const
{
    std::lock_guard lock(mutex_);
    for (const auto &entry : entries_)
        if (!entry->job.finished())
            return true;
    return false;
}

void Installer::clear_finished()
{
    std::lock_guard lock(mutex_);
    std::deque<std::shared_ptr<Entry>> kept;
    for (auto &entry : entries_)
        if (!entry->job.finished())
            kept.push_back(entry);
    entries_.swap(kept);
}

void Installer::update(int id, const std::function<void(Job &)> &change)
{
    std::lock_guard lock(mutex_);
    for (auto &entry : entries_)
        if (entry->job.id == id)
            change(entry->job);
}

void Installer::run()
{
    for (;;)
    {
        std::shared_ptr<Entry> next;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] {
                if (stopping_)
                    return true;
                for (auto &entry : entries_)
                    if (entry->job.state == JobState::Queued)
                        return true;
                return false;
            });
            if (stopping_)
                return;
            for (auto &entry : entries_)
                if (entry->job.state == JobState::Queued)
                {
                    next = entry;
                    break;
                }
        }
        if (next)
            process(*next);
    }
}

void Installer::process(Entry &entry)
{
    std::string error;
    const bool ok = entry.job.kind == JobKind::Install ? do_install(entry, error) : do_uninstall(entry, error);
    update(entry.job.id, [&](Job &job) {
        if (ok)
            job.state = JobState::Done;
        else if (entry.cancel->requested.load())
            job.state = JobState::Cancelled;
        else
        {
            job.state = JobState::Failed;
            job.error = error;
        }
    });
    platform::log("[install] job %d %s: %s", entry.job.id, ok ? "done" : "failed", ok ? "" : error.c_str());
    generation_.fetch_add(1);
}

bool Installer::do_install(Entry &entry, std::string &error)
{
    const int id = entry.job.id;
    Game &game = entry.game;
    update(id, [](Job &job) { job.state = JobState::Resolving; });
    if (game.access != Access::Install)
    {
        error = "Dieser Eintrag ist nicht als PS5-Titel installierbar";
        return false;
    }
    if (!game.resolved && !resolve_(game, error))
        return false;
    // PS5 titles (native code) need a published SHA-256. Library games may come from a store
    // that publishes none (itch.io): they are accepted over HTTPS with the size the store
    // states, because an integrated runtime only interprets their data and never executes
    // a program from the download (finish_library refuses anything else).
    const bool unhashed_ok = entry.job.library && game.sha256.empty() && game.size > 0;
    if (!is_https_url(game.artifact_url) || (game.sha256.size() != 64 && !unhashed_ok))
    {
        error = "Es gibt keinen geprüften Download für diesen Eintrag";
        return false;
    }
    const std::string root = entry.job.root;
    if (!entry.job.component.empty() && (!component_spec(entry.job.component) || game.access != Access::Install))
    {
        error = "Unbekannte Runtime-Komponente";
        return false;
    }
    if (entry.job.library && !valid_library_id(game.profile))
    {
        error = "Der Katalogeintrag hat keine gültige Bibliotheks-ID";
        return false;
    }
    if (!fs::is_dir(root) && !fs::make_dirs(root))
    {
        error = "Der Installationsordner " + root + " ist nicht erreichbar";
        return false;
    }
    if (!fs::writable(root))
    {
        error = "Kein Schreibzugriff auf " + root +
                ". Auf der PS5 braucht Akeno ShadowMountPlus 1.7 oder neuer, das /data in die App einbindet.";
        return false;
    }
    const std::string work = work_dir_for(root);
    const std::string job_dir = fs::join(work, "job-" + std::to_string(id) + "-" + std::to_string(platform::unix_time()));
    fs::remove_tree(job_dir);
    if (!fs::make_dirs(job_dir))
    {
        error = "Der Arbeitsordner kann nicht angelegt werden";
        return false;
    }
    struct Cleanup
    {
        std::string path;
        ~Cleanup() { fs::remove_tree(path); }
    } cleanup{job_dir};

    if (game.size)
        if (auto space = fs::free_space(work); space && *space < game.size * 2 + 64ull * 1024 * 1024)
        {
            error = "Zu wenig freier Speicher: " + format_bytes(game.size * 2) + " benötigt, " +
                    format_bytes(*space) + " frei";
            return false;
        }

    // Download, hashing as the bytes arrive. Up to three attempts; a later attempt continues
    // after the bytes already saved (HTTP Range) and starts over if the server sends it all.
    const std::string archive = fs::join(job_dir, "download.zip");
    const int file = ::open(archive.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0)
    {
        error = "Die Download-Datei kann nicht angelegt werden";
        return false;
    }
    auto hash = std::make_unique<Sha256>();
    std::uint64_t received = 0;
    const auto started = std::chrono::steady_clock::now();
    auto last_report = started;
    update(id, [&](Job &job) {
        job.state = JobState::Downloading;
        job.total = game.size;
        job.title_id = game.title_id;
    });
    bool write_failed = false;
    net::Response response;
    for (int attempt = 0; attempt < 3 && !entry.cancel->requested.load(); ++attempt)
    {
        if (attempt > 0)
        {
            update(id, [&](Job &job) { job.note = "Neuer Versuch " + std::to_string(attempt + 1) + " von 3"; });
            std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
        }
        const std::uint64_t resume_at = received;
        bool first_chunk = true;
        response = http_.download(
            game.artifact_url,
            [&](std::string_view chunk, std::uint64_t total, int status) {
                if (first_chunk)
                {
                    first_chunk = false;
                    if (resume_at > 0 && status != 206 && received == resume_at)
                    {
                        // The server ignored the range: start the file and the hash again.
                        if (::ftruncate(file, 0) != 0 || ::lseek(file, 0, SEEK_SET) != 0)
                        {
                            write_failed = true;
                            return false;
                        }
                        hash = std::make_unique<Sha256>();
                        received = 0;
                    }
                }
                std::size_t put = 0;
                while (put < chunk.size())
                {
                    const ssize_t n = ::write(file, chunk.data() + put, chunk.size() - put);
                    if (n < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        write_failed = true;
                        return false;
                    }
                    put += static_cast<std::size_t>(n);
                }
                hash->update(chunk.data(), chunk.size());
                received += chunk.size();
                const auto now = std::chrono::steady_clock::now();
                if (now - last_report > std::chrono::milliseconds(250))
                {
                    last_report = now;
                    const double seconds = std::chrono::duration<double>(now - started).count();
                    update(id, [&](Job &job) {
                        job.done = received;
                        job.total = game.size ? game.size : (total ? total + resume_at : 0);
                        job.bytes_per_second = seconds > 0 ? static_cast<double>(received) / seconds : 0;
                    });
                }
                return true;
            },
            entry.cancel.get(), game.size ? game.size + 1024 * 1024 : 8ull * 1024 * 1024 * 1024, resume_at);
        const bool complete = response.error.empty() && (response.status == 200 || response.status == 206) &&
                              (game.size == 0 || received == game.size);
        // Retry only an interrupted transfer; a finished one with the wrong size will not improve.
        const bool finished = response.error.empty() && (response.status == 200 || response.status == 206);
        if (complete || finished || write_failed || entry.cancel->requested.load() ||
            (response.error.empty() && response.status >= 400 && response.status < 500 && response.status != 408 &&
             response.status != 429))
            break;
        platform::log("[install] attempt %d for %s ended at %llu bytes: HTTP %d %s", attempt + 1, game.id.c_str(),
                      static_cast<unsigned long long>(received), response.status, response.error.c_str());
    }
    ::fsync(file);
    ::close(file);
    update(id, [&](Job &job) {
        job.done = received;
        job.note.clear();
    });
    if (!response.error.empty() || (response.status != 200 && response.status != 206) ||
        (game.size && received != game.size))
    {
        error = write_failed ? "Der Download konnte nicht gespeichert werden (Speicher voll?)"
                : !response.error.empty() ? response.error
                : response.status != 200 && response.status != 206
                    ? "Der Download antwortete mit HTTP " + std::to_string(response.status)
                    : "Der Download ist unvollständig (" + format_bytes(received) + " von " + format_bytes(game.size) + ")";
        return false;
    }
    update(id, [](Job &job) { job.state = JobState::Verifying; });
    const std::string digest = hash->hex();
    if (!unhashed_ok && !digest_equal(digest, game.sha256))
    {
        error = "SHA-256 stimmt nicht: Der Download entspricht nicht dem Katalog. Nichts wurde installiert.";
        return false;
    }
    if (!entry.job.component.empty())
        return finish_component(entry, archive, job_dir, digest, error);
    if (entry.job.library)
        return finish_library(entry, archive, job_dir, digest, error);
    std::string zip_error;
    const auto folder = zip::find_title_folder(archive, zip_error);
    if (!folder)
    {
        error = zip_error;
        return false;
    }
    ParamInfo info;
    if (!parse_param_json(folder->param_json, info))
    {
        error = "sce_sys/param.json im Archiv ist ungültig";
        return false;
    }
    if (!game.title_id.empty() && info.title_id != game.title_id)
    {
        error = "Das Archiv enthält " + info.title_id + " statt " + game.title_id;
        return false;
    }
    const std::string title_id = info.title_id;
    update(id, [&](Job &job) {
        job.title_id = title_id;
        job.state = JobState::Unpacking;
        job.done = 0;
        job.total = folder->bytes;
    });
    const std::string unpacked = fs::join(job_dir, title_id);
    if (!zip::extract(archive, folder->prefix, unpacked,
                      [&](std::uint64_t done, std::uint64_t total) {
                          update(id, [&](Job &job) {
                              job.done = done;
                              job.total = total;
                          });
                          return !entry.cancel->requested.load();
                      },
                      error))
        return false;
    fs::remove_tree(archive);

    update(id, [](Job &job) { job.state = JobState::Finishing; });
    if (title_running(title_id))
    {
        error = "Der Titel läuft gerade. Schließe ihn und versuche es erneut.";
        return false;
    }
    const std::string destination = fs::join(root, title_id);
    const std::string previous_dir = fs::join(work, "previous");
    const std::string previous = fs::join(previous_dir, title_id);
    bool replaced = false;
    if (fs::exists(destination))
    {
        fs::make_dirs(previous_dir);
        fs::remove_tree(previous);
        if (!fs::rename(destination, previous))
        {
            error = "Die vorhandene Version kann nicht beiseitegelegt werden";
            return false;
        }
        replaced = true;
    }
    if (!fs::rename(unpacked, destination))
    {
        if (replaced)
            fs::rename(previous, destination); // put the old version back
        error = "Der neue Titelordner kann nicht aktiviert werden";
        return false;
    }
    fs::chmod_tree(destination, 0777); // the console starts only titles readable by every user
    library_.write_receipt(title_id, game.id, game.content_version.empty() ? game.version : game.content_version,
                           digest, destination);
    if (replaced)
        update(id, [](Job &job) { job.note = "Vorherige Version in akeno-work/previous gesichert"; });
    return true;
}

bool Installer::do_uninstall(Entry &entry, std::string &error)
{
    update(entry.job.id, [](Job &job) { job.state = JobState::Finishing; });
    if (entry.job.library)
        return remove_profile(entry.job.profile, false, error);
    const std::string folder = entry.job.root;
    const std::string root = fs::parent(folder);
    if (title_running(entry.job.title_id))
    {
        error = "Der Titel läuft gerade. Schließe ihn zuerst.";
        return false;
    }
    if (!fs::is_file(fs::join(folder, "sce_sys/param.json")))
    {
        error = "Der Ordner ist kein PS5-Titel";
        return false;
    }
    const std::string trash = fs::join(work_dir_for(root), "trash-" + std::to_string(entry.job.id));
    fs::make_dirs(work_dir_for(root));
    if (!fs::rename(folder, trash))
    {
        error = "Der Titelordner kann nicht entfernt werden (Schreibzugriff?)";
        return false;
    }
    fs::remove_tree(trash);
    library_.remove_receipt(entry.job.title_id);
    return true;
}

// ---------------------------------------------------------------------------------------
// Shared runtime components

bool Installer::finish_component(Entry &entry, const std::string &archive, const std::string &job_dir,
                                 const std::string &digest, std::string &error)
{
    const int id = entry.job.id;
    const ComponentSpec *spec = component_spec(entry.job.component);
    const Game &game = entry.game;
    std::string zip_error;
    // The source is a PS5 title ZIP; the runtime is a subtree of its title folder.
    const auto folder = zip::find_title_folder(archive, zip_error);
    if (!folder)
    {
        error = zip_error;
        return false;
    }
    const std::string keep = spec->keep;
    const std::string version = sanitize_text(game.content_version.empty() ? game.version : game.content_version, 60);
    const std::string dir = fs::join(components_root(), spec->id);
    const std::string staging = fs::join(job_dir, "component");
    update(id, [&](Job &job) {
        job.state = JobState::Unpacking;
        job.done = 0;
        job.total = folder->bytes;
    });
    if (!zip::extract(archive, folder->prefix + keep, staging,
                      [&](std::uint64_t done, std::uint64_t total) {
                          update(id, [&](Job &job) {
                              job.done = done;
                              job.total = total;
                          });
                          return !entry.cancel->requested.load();
                      },
                      error))
        return false;
    fs::remove_tree(archive);
    const std::string marker = std::string(spec->marker).substr(keep.size());
    if (!fs::is_file(fs::join(staging, marker)))
    {
        error = std::string("Das Paket enthält die erwartete Datei ") + spec->marker + " nicht. Nichts wurde installiert.";
        return false;
    }
    update(id, [](Job &job) { job.state = JobState::Finishing; });
    const std::string destination = fs::join(dir, version.empty() ? "current" : version);
    fs::make_dirs(dir);
    fs::remove_tree(destination);
    if (!fs::rename(staging, destination))
    {
        error = "Die Runtime kann nicht nach " + dir + " verschoben werden";
        return false;
    }
    InstalledComponent record;
    record.id = spec->id;
    record.version = version;
    record.path = destination;
    record.source = game.id;
    record.sha256 = digest;
    record.installed = platform::unix_time();
    if (!write_component_record(record))
    {
        error = "Der Eintrag der Runtime kann nicht geschrieben werden";
        return false;
    }
    // Older versions go once the new one is recorded.
    for (const fs::Entry &old : fs::list(dir))
        if (old.directory && fs::join(dir, old.name) != destination)
            fs::remove_tree(fs::join(dir, old.name));
    update(id, [&](Job &job) { job.note = std::string(spec->name) + " " + version + " · von allen Spielen geteilt"; });
    return true;
}

// ---------------------------------------------------------------------------------------
// Games for Akeno's own library

bool Installer::finish_library(Entry &entry, const std::string &archive, const std::string &job_dir,
                               const std::string &digest, std::string &error)
{
    const int id = entry.job.id;
    const Game &game = entry.game;
    std::string zip_error;
    const auto contents = zip::inspect(archive, zip_error);
    if (!contents)
    {
        error = zip_error;
        return false;
    }
    update(id, [&](Job &job) {
        job.state = JobState::Unpacking;
        job.done = 0;
        job.total = contents->bytes;
    });
    const std::string unpacked = fs::join(job_dir, "game");
    if (!zip::extract(archive, contents->prefix, unpacked,
                      [&](std::uint64_t done, std::uint64_t total) {
                          update(id, [&](Job &job) {
                              job.done = done;
                              job.total = total;
                          });
                          return !entry.cancel->requested.load();
                      },
                      error))
        return false;
    fs::remove_tree(archive);

    // Engine, architecture and runtime come from the files themselves, not from the store.
    update(id, [](Job &job) {
        job.state = JobState::Finishing;
        job.note = "Engine wird erkannt";
    });
    const Detection detection = detect_game(unpacked);
    if (detection.engine.empty())
    {
        error = "Im Download wurde kein bekanntes Spielformat gefunden. Nichts wurde installiert.";
        return false;
    }
    if (!detection.playable)
    {
        error = "Erkannt: " + detection.engine_name + " (" + detection.format + "). " + detection.advice +
                ". Nichts wurde installiert.";
        return false;
    }
    const std::string folder_rel =
        detection.root.size() > unpacked.size() ? detection.root.substr(unpacked.size() + 1) : std::string();
    const std::string dir = library_dir(game.profile);
    const std::string destination = fs::join(dir, "game");
    const std::string previous = fs::join(dir, "game.previous");
    fs::make_dirs(dir);
    fs::remove_tree(previous);
    const bool replaced = fs::exists(destination);
    if (replaced && !fs::rename(destination, previous))
    {
        error = "Die vorhandene Version kann nicht beiseitegelegt werden";
        return false;
    }
    if (!fs::rename(unpacked, destination))
    {
        if (replaced)
            fs::rename(previous, destination);
        error = "Der Spielordner kann nicht in die Bibliothek verschoben werden";
        return false;
    }
    GameProfile profile;
    if (const auto existing = read_profile(game.profile))
        profile = *existing; // keeps last_played and the saves folder
    profile.id = game.profile;
    profile.title = game.title;
    profile.developer = game.developer;
    profile.engine = detection.engine;
    profile.runtime = detection.runtime;
    profile.folder = folder_rel.empty() ? destination : fs::join(destination, folder_rel);
    profile.saves = fs::join(dir, "saves");
    profile.source = game.id;
    profile.version = game.content_version.empty() ? game.version : game.content_version;
    profile.cover = !game.cover_url.empty() ? game.cover_url : game.icon_url;
    profile.sha256 = digest;
    profile.owned_files = true;
    profile.installed = platform::unix_time();
    fs::make_dirs(profile.saves);
    if (!write_profile(profile))
    {
        fs::remove_tree(destination);
        if (replaced)
            fs::rename(previous, destination);
        error = "Das Startprofil kann nicht geschrieben werden";
        return false;
    }
    fs::remove_tree(previous);
    update(id, [&](Job &job) {
        job.note = detection.engine_name + " · startet in Akeno (" + detection.runtime + ")";
    });
    return true;
}

} // namespace akeno
