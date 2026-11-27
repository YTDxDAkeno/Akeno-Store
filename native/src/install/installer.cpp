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
    entry->job.windows = game.runtime == "windows";
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
    entry->job.root = title.windows ? title.root : title.folder;
    entry->job.game_id = title.source_game_id;
    entry->job.windows = title.windows;
    entry->job.profile = title.profile;
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
    if (!is_https_url(game.artifact_url) || game.sha256.size() != 64)
    {
        error = "Es gibt keinen geprüften Download für diesen Eintrag";
        return false;
    }
    const std::string root = entry.job.root;
    if (entry.job.windows)
    {
        // Into MihawkOS's own library: its C: drive must exist (MihawkOS creates it on its first start).
        if (!valid_mihawk_profile(game.profile) || game.folder_name.empty() || game.programs.empty())
        {
            error = "Der Katalogeintrag beschreibt kein Windows-Spiel für MihawkOS";
            return false;
        }
        if (!fs::is_dir(mihawk_drive_c(root)))
        {
            error = "MihawkOS hat sein Laufwerk C: noch nicht angelegt (" + mihawk_drive_c(root) +
                    "). Starte MihawkOS einmal und versuche es dann erneut.";
            return false;
        }
    }
    else if (!fs::is_dir(root) && !fs::make_dirs(root))
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
        if (complete || write_failed || entry.cancel->requested.load() ||
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
    if (!digest_equal(digest, game.sha256))
    {
        error = "SHA-256 stimmt nicht: Der Download entspricht nicht dem Katalog. Nichts wurde installiert.";
        return false;
    }
    if (entry.job.windows)
        return finish_windows(entry, archive, job_dir, digest, error);
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
    if (entry.job.windows)
        return uninstall_windows(entry, error);
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
// Windows games in MihawkOS's library

namespace
{

// 0x8664 (x86-64), 0x14c (x86, also .NET "Any CPU") or 0 when the file is no PE program.
unsigned pe_machine(const std::string &path)
{
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0)
        return 0;
    unsigned char header[1024] = {};
    const ssize_t got = ::read(file, header, sizeof header);
    ::close(file);
    if (got < 0x40 || header[0] != 'M' || header[1] != 'Z')
        return 0;
    const std::uint32_t pe = header[0x3c] | header[0x3d] << 8 | header[0x3e] << 16 | static_cast<std::uint32_t>(header[0x3f]) << 24;
    if (pe + 6 > static_cast<std::uint32_t>(got) || header[pe] != 'P' || header[pe + 1] != 'E' || header[pe + 2] ||
        header[pe + 3])
        return 0;
    const unsigned machine = header[pe + 4] | header[pe + 5] << 8;
    return machine == 0x8664 || machine == 0x14c ? machine : 0;
}

bool helper_program(const std::string &name)
{
    // Uninstallers, crash handlers, setup and redistributable installers are never the game.
    const std::string n = lower(name);
    return starts_with(n, "unins") || starts_with(n, "unitycrashhandler") || n.find("setup") != std::string::npos ||
           n.find("redist") != std::string::npos || starts_with(n, "vc_") || starts_with(n, "dxsetup") ||
           n.find("crashpad") != std::string::npos || n.find("crashreport") != std::string::npos;
}

void collect_programs(const std::string &folder, const std::string &relative, int depth,
                      std::vector<std::string> &out)
{
    if (depth > 3 || out.size() > 200)
        return;
    for (const fs::Entry &entry : fs::list(folder))
    {
        const std::string path = fs::join(folder, entry.name);
        const std::string rel = relative.empty() ? entry.name : relative + "/" + entry.name;
        if (entry.directory)
            collect_programs(path, rel, depth + 1, out);
        else if (ends_with(lower(entry.name), ".exe") && !helper_program(entry.name) && pe_machine(path))
            out.push_back(rel);
    }
}

std::string windows_path(const std::string &folder_name, const std::string &relative)
{
    std::string path = "C:\\Games\\" + folder_name;
    if (!relative.empty())
        path += "\\" + relative;
    for (char &c : path)
        if (c == '/')
            c = '\\';
    return path;
}

std::size_t depth(const std::string &relative)
{
    std::size_t n = 0;
    for (char c : relative)
        n += c == '/';
    return n;
}

} // namespace

bool Installer::find_windows_program(const std::string &folder, const std::vector<std::string> &preferred,
                                     std::string &relative, std::string &error)
{
    std::vector<std::string> programs;
    collect_programs(folder, "", 0, programs);
    std::sort(programs.begin(), programs.end(), [](const std::string &a, const std::string &b) {
        return depth(a) != depth(b) ? depth(a) < depth(b) : a < b;
    });
    for (const std::string &wanted : preferred)
        for (const std::string &program : programs)
            if (lower(fs::leaf(program)) == lower(wanted))
            {
                relative = program;
                return true;
            }
    if (programs.size() == 1)
    {
        relative = programs.front();
        return true;
    }
    std::string found;
    for (std::size_t i = 0; i < programs.size() && i < 5; ++i)
        found += (i ? ", " : "") + fs::leaf(programs[i]);
    error = programs.empty() ? "Das Archiv enthält kein Windows-Programm für x86 oder x86-64"
                             : "Das Archiv enthält kein eindeutiges Startprogramm (gefunden: " + found + ")";
    return false;
}

std::string Installer::mihawk_game_json(const Game &game, const std::string &relative_program, const std::string &sha256)
{
    const std::string program = windows_path(game.folder_name, relative_program);
    const std::size_t slash = relative_program.rfind('/');
    const std::string directory =
        windows_path(game.folder_name, slash == std::string::npos ? std::string() : relative_program.substr(0, slash));
    const std::string version = game.content_version.empty() ? game.version : game.content_version;
    return "{\n  \"name\": " + json::quote(game.title) + ",\n  \"detail\": " +
           json::quote("Installiert mit Akeno Gaming Hub" + (version.empty() ? std::string() : " · " + version)) +
           ",\n  \"executable\": " + json::quote(program) + ",\n  \"directory\": " + json::quote(directory) +
           ",\n  \"akeno\": {\"schema\": 1, \"game\": " + json::quote(game.id) + ", \"version\": " +
           json::quote(version) + ", \"sha256\": " + json::quote(sha256) + ", \"folder\": " +
           json::quote(game.folder_name) + ", \"installed\": " + std::to_string(platform::unix_time()) + "}\n}\n";
}

bool Installer::finish_windows(Entry &entry, const std::string &archive, const std::string &job_dir,
                               const std::string &digest, std::string &error)
{
    const int id = entry.job.id;
    const Game &game = entry.game;
    const std::string root = entry.job.root;
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
    std::string program;
    if (!find_windows_program(unpacked, game.programs, program, error))
        return false;

    update(id, [](Job &job) { job.state = JobState::Finishing; });
    if (title_running("PPSA99170"))
    {
        error = "MihawkOS läuft gerade. Schließe es und versuche es erneut.";
        return false;
    }
    const std::string games_dir = fs::join(mihawk_drive_c(root), "Games");
    if (!fs::is_dir(games_dir) && !fs::make_dirs(games_dir))
    {
        error = "C:\\Games kann in MihawkOS nicht angelegt werden";
        return false;
    }
    const std::string destination = fs::join(games_dir, game.folder_name);
    const std::string entry_dir = fs::join(fs::join(root, "games"), game.profile);
    // Replace only what Akeno installed itself: the library entry must name this folder.
    if (fs::exists(destination))
    {
        bool ours = false;
        if (const auto text = fs::read_file(fs::join(entry_dir, "game.json"), 256 * 1024))
        {
            const json::Document doc = json::Document::parse(*text);
            ours = doc.root()["akeno"].str("folder") == game.folder_name;
        }
        if (!ours)
        {
            error = "C:\\Games\\" + game.folder_name + " gibt es schon und stammt nicht von Akeno. Nichts wurde verändert.";
            return false;
        }
    }
    const std::string work = work_dir_for(root);
    const std::string previous_dir = fs::join(work, "previous");
    const std::string previous = fs::join(previous_dir, "windows-" + game.profile);
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
            fs::rename(previous, destination);
        error = "Der Spielordner kann nicht nach C:\\Games verschoben werden";
        return false;
    }
    fs::chmod_tree(destination, 0777); // MihawkOS runs as another user and must read it
    if (!fs::make_dirs(entry_dir) || !fs::write_atomic(fs::join(entry_dir, "game.json"), mihawk_game_json(game, program, digest)))
    {
        fs::remove_tree(destination);
        if (replaced)
            fs::rename(previous, destination);
        error = "Der Eintrag in der MihawkOS-Bibliothek kann nicht geschrieben werden";
        return false;
    }
    fs::chmod_tree(entry_dir, 0777);
    update(id, [&](Job &job) {
        job.note = "In MihawkOS: " + windows_path(game.folder_name, program) +
                   (replaced ? " (vorherige Version in akeno-work/previous)" : "");
    });
    return true;
}

bool Installer::uninstall_windows(Entry &entry, std::string &error)
{
    const std::string root = entry.job.root;
    if (!valid_mihawk_profile(entry.job.profile))
    {
        error = "Ungültiger MihawkOS-Eintrag";
        return false;
    }
    if (title_running("PPSA99170"))
    {
        error = "MihawkOS läuft gerade. Schließe es zuerst.";
        return false;
    }
    const std::string entry_dir = fs::join(fs::join(root, "games"), entry.job.profile);
    const auto text = fs::read_file(fs::join(entry_dir, "game.json"), 256 * 1024);
    const json::Document doc = text ? json::Document::parse(*text) : json::Document();
    const std::string folder = doc.root()["akeno"].str("folder");
    if (folder.empty() || folder.find('/') != std::string::npos || folder.find('\\') != std::string::npos ||
        folder == "." || folder == "..")
    {
        error = "Dieses Spiel wurde nicht mit Akeno installiert; entferne es in MihawkOS";
        return false;
    }
    const std::string destination = fs::join(fs::join(mihawk_drive_c(root), "Games"), folder);
    const std::string work = work_dir_for(root);
    fs::make_dirs(work);
    if (fs::exists(destination))
    {
        const std::string trash = fs::join(work, "trash-" + std::to_string(entry.job.id));
        if (!fs::rename(destination, trash))
        {
            error = "Der Spielordner kann nicht entfernt werden (Schreibzugriff?)";
            return false;
        }
        fs::remove_tree(trash);
    }
    fs::remove_tree(entry_dir);
    update(entry.job.id, [](Job &job) { job.note = "Spielstände unter C:\\users in MihawkOS bleiben erhalten"; });
    return true;
}

} // namespace akeno
