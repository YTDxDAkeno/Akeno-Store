// Akeno Gaming Hub - host tests for the native core (no network, no console).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fs.hpp"
#include "core/hash.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "core/zip.hpp"
#include "install/installer.hpp"
#include "install/library.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "runtime/launch.hpp"
#include "runtime/runtimes.hpp"
#include "stores/catalog.hpp"
#include "stores/github.hpp"
#include "stores/provider.hpp"

#include "miniz.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
#include <unistd.h>

using namespace akeno;

static int failures = 0, checks = 0;
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ++failures;                                                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                              \
    } while (0)

static std::string fixture(const std::string &name)
{
    const auto data = fs::read_file(std::string(AKENO_FIXTURES) + "/" + name, 64 * 1024 * 1024);
    if (!data)
        std::fprintf(stderr, "missing fixture %s\n", name.c_str());
    return data.value_or("");
}

static std::string temp_dir(const char *name)
{
    char path[] = "/tmp/akeno-test-XXXXXX";
    const char *made = ::mkdtemp(path);
    return fs::join(made ? made : "/tmp", name);
}

// ---------------------------------------------------------------------------------------
static void test_util()
{
    CHECK(lower("PPSA-Äb") == "ppsa-Äb");
    CHECK(trim("  a b \n") == "a b");
    CHECK(is_title_id("PPSA99874") && !is_title_id("ppsa99874") && !is_title_id("PPSA9987"));
    bool comparable = false;
    CHECK(compare_content_version("01.000.070", "01.000.060", &comparable) == 1 && comparable);
    CHECK(compare_content_version("01.000.070", "01.000.070", &comparable) == 0 && comparable);
    CHECK(compare_content_version("01.000.070", "01.000.080", &comparable) == -1);
    compare_content_version("v1.0.7", "01.000.000", &comparable);
    CHECK(!comparable);
    CHECK(strip_html("<p>Hallo&nbsp;<b>Welt</b> &amp; &#252;</p>") == "Hallo\xC2\xA0 Welt & \xC3\xBC");
    CHECK(sanitize_text("a\x01" "b\tc", 10) == "ab c");
    CHECK(sanitize_text("\xC3\xA4\xC3\xB6", 3) == "\xC3\xA4"); // never cuts inside a character
    CHECK(host_of("https://User@Example.com/x") == "");
    CHECK(host_of("https://Example.COM:443/x?y") == "example.com");
    CHECK(is_https_url("https://prosperostore.net/api/v1/") && !is_https_url("http://a.b/") &&
          !is_https_url("https://a b.c/") && !is_https_url("https://localhost/"));
    CHECK(net::resolve_location("https://a.com/x/y?z", "/r") == "https://a.com/r");
    CHECK(net::resolve_location("https://a.com/x/y", "q") == "https://a.com/x/q");
    CHECK(glob_match("MaxPayne3-RePrescribed-*.zip", "MaxPayne3-RePrescribed-1.2.zip"));
    CHECK(glob_match("Porpoise-*.?.zip", "Porpoise-1.4.zip") && !glob_match("Porpoise-*.?.zip", "Porpoise-1.40.zip"));
    CHECK(format_bytes(1536) == "2 KB" && format_bytes(5ull * 1024 * 1024) == "5.0 MB");
}

static void test_hash()
{
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 streaming;
    streaming.update("a");
    streaming.update("bc");
    CHECK(streaming.hex() == sha256_hex("abc"));
    CHECK(digest_equal("ABCDEF", "abcdef") && !digest_equal("abc", "abd") && !digest_equal("", ""));
    // RFC 8032, section 7.1, tests 1 and 2.
    std::uint8_t key[32], sig[64];
    CHECK(from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key, 32));
    CHECK(from_hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b4"
                   "6bd25bf5f0595bbe24655141438e7a100b",
                   sig, 64));
    CHECK(ed25519_verify(key, sig, ""));
    CHECK(!ed25519_verify(key, sig, "x"));
    CHECK(from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", key, 32));
    CHECK(from_hex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c"
                   "387b2eaeb4302aeeb00d291612bb0c00",
                   sig, 64));
    CHECK(ed25519_verify(key, sig, "\x72"));
}

static void test_prosperostore()
{
    const std::string manifest = fixture("prosperostore/manifest.json");
    const std::string signature = fixture("prosperostore/manifest.sig");
    const std::string index = fixture("prosperostore/index.json");
    std::string error;
    std::int64_t sequence = 0;
    CHECK(parse::prosperostore_manifest(manifest, signature, error, &sequence));
    CHECK(sequence == 1293);
    std::string tampered = manifest;
    tampered[tampered.size() / 2] ^= 1;
    CHECK(!parse::prosperostore_manifest(tampered, signature, error, nullptr));
    // index.json must hash to the manifest's entry
    const json::Document doc = json::Document::parse(manifest);
    CHECK(digest_equal(sha256_hex(index), doc.root()["files"].str("index.json")));
    std::vector<Game> games;
    CHECK(parse::prosperostore_index(index, games, error));
    CHECK(games.size() > 230);
    int installable = 0, upcoming = 0;
    bool craft = false, pokemmo = false, web = false;
    for (const Game &game : games)
    {
        installable += game.access == Access::Install;
        upcoming += game.access == Access::Upcoming;
        craft |= game.title_id == "PPSA99874";
        pokemmo |= game.title_id == "PPSA98001";
        web |= game.title_id == "PRWB00001";
        CHECK(game.signed_catalog && game.id.rfind("prosperostore:", 0) == 0);
    }
    CHECK(installable > 200 && upcoming > 0);
    CHECK(craft && pokemmo && web);

    Game game;
    game.title_id = "PPSA99999";
    const std::string app = R"({"schema":3,"titleid":"PPSA99999","id":"PPSA99999","name":"Demo","status":"available",
      "format":"zip","artifact_url":"https://github.com/o/r/releases/download/v1/PPSA99999.zip",
      "sha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","license":"MIT",
      "content_version":"01.000.002","kind":"game"})";
    CHECK(parse::prosperostore_app(app, game, error));
    CHECK(game.sha256 == std::string(64, 'a') && game.content_version == "01.000.002");
    Game other;
    other.title_id = "PPSA11111";
    CHECK(!parse::prosperostore_app(app, other, error));
}

static void test_other_stores()
{
    std::string error;
    std::vector<Game> recomp;
    const std::string catalog = R"({"catalogVersion":3,"v2Apps":[
      {"id":"PPSA95454","name":"Max Payne 3","author":"ChaosTheory","category":"games","needs":"Xbox 360 ISO",
       "icon":"https://recompstore.com/i.png","hero":"https://recompstore.com/h.jpg",
       "source":{"type":"github","repo":"ChaosTheory1075/mp3-ps5","asset":"MaxPayne3-*.zip"}},
      {"id":"PPSA99203","name":"PS5SX2","category":"emulators","source":{"type":"github","repo":"Swordpdf/PS5SX2","asset":"PS5SX2-*.zip"}}],
      "apps":[{"id":"PPSA95454","name":"duplicate"}],"comingSoonV2":[{"id":"PPSA12121","name":"Soon"}]})";
    CHECK(parse::recompstore_catalog(catalog, recomp, error));
    CHECK(recomp.size() == 3);
    CHECK(recomp[0].needs == "Xbox 360 ISO" && recomp[0].has_tag("bring-your-own") && recomp[0].access == Access::Install);
    CHECK(recomp[1].has_tag("emulator") && recomp[2].access == Access::Upcoming);

    Game asset;
    asset.title_id = "PPSA95454";
    const std::string release = R"({"tag_name":"v1.2","body":"Notes","assets":[
      {"name":"source.tar.gz","browser_download_url":"https://github.com/x/y/s.tgz","size":1},
      {"name":"MaxPayne3-RePrescribed-1.2.zip","browser_download_url":"https://github.com/x/y/m.zip","size":42,
       "digest":"sha256:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"}]})";
    CHECK(parse::github_release(release, "MaxPayne3-*.zip", asset, error));
    CHECK(asset.sha256 == std::string(64, 'b') && asset.size == 42 && asset.version == "v1.2");
    Game no_digest;
    CHECK(!parse::github_release(R"({"assets":[{"name":"PPSA1.zip","browser_download_url":"https://a.b/c","size":3}]})",
                                 "PPSA1.zip", no_digest, error));
    Game mismatch;
    mismatch.sha256 = std::string(64, 'c');
    CHECK(!parse::github_release(release, "MaxPayne3-*.zip", mismatch, error));

    std::vector<Game> itch;
    const std::string rss = R"(<?xml version="1.0"?><rss version="2.0"><channel>
      <item><title>Web Game [Free] [Puzzle]</title><plainTitle>Web Game</plainTitle><link>https://dev.itch.io/web-game</link>
        <imageurl>https://img.itch.zone/a.png</imageurl><description><![CDATA[<p>Fun &amp; fast</p>]]></description>
        <platforms><html>yes</html></platforms></item>
      <item><title>Story [Free] [Visual Novel]</title><plainTitle>Story</plainTitle><link>https://vn.itch.io/story</link>
        <platforms><windows>yes</windows></platforms></item>
      <item><title>Spicy NSFW thing</title><link>https://x.itch.io/s</link></item>
      <item><title>Bad link</title><link>https://evil.example/x</link></item></channel></rss>)";
    CHECK(parse::itch_feed(rss, false, itch, error));
    CHECK(itch.size() == 2);
    CHECK(itch[0].access == Access::Runtime && itch[0].runtime == "web" && itch[0].description == "Fun & fast");
    CHECK(itch[1].access == Access::External && itch[1].has_tag("visual-novel") && itch[1].runtime == "windows");
    CHECK(!parse::itch_feed("<!DOCTYPE x [<!ENTITY a 'b'>]><rss></rss>", false, itch, error));

    std::vector<Game> gj;
    const std::string jolt = R"({"payload":{"games":[
      {"id":7,"slug":"a","title":"Browser Jam","developer":{"display_name":"Dev"},"compatibility":{"type_html":true},
       "thumbnail_media_item":{"filetype":"image/png","img_url":"https://i.gjcdn.net/a.png"},"sellable":{"type":"free"}},
      {"id":8,"slug":"b","title":"Adult","has_adult_content":true}]}})";
    CHECK(parse::gamejolt_discover(jolt, false, gj, error));
    CHECK(gj.size() == 1 && gj[0].access == Access::Runtime && gj[0].launch_url == "https://gamejolt.com/games/a/7");

    std::vector<Game> gog;
    const std::string gogjson = R"({"products":[{"id":"1","title":"Free One","developers":["Studio"],
      "coverVertical":"https://images.gog-statics.com/v.jpg","storeLink":"https://www.gog.com/en/game/free_one",
      "operatingSystems":["windows"],"price":{"final":"$0.00","finalMoney":{"amount":"0.00"}},
      "features":[{"slug":"single"}],"screenshots":["https://images.gog-statics.com/s_{formatter}.jpg"]}]})";
    CHECK(parse::gog_catalog(gogjson, gog, error));
    CHECK(gog.size() == 1 && gog[0].free && gog[0].runtime == "windows" && gog[0].has_tag("singleplayer"));
    CHECK(gog[0].screenshots.size() == 1 && gog[0].screenshots[0].find("{formatter}") == std::string::npos);

    std::vector<Game> epic;
    const std::string epicjson = R"({"data":{"Catalog":{"searchStore":{"elements":[
      {"title":"Free Now","id":"e1","price":{"totalPrice":{"discountPrice":0,"originalPrice":1349}},
       "catalogNs":{"mappings":[{"pageSlug":"free-now","pageType":"productHome"}]},
       "promotions":{"promotionalOffers":[{"promotionalOffers":[{"startDate":"2026-11-20T16:00:00.000Z",
         "endDate":"2026-12-04T16:00:00.000Z","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}]}},
      {"title":"Paid","id":"e2","price":{"totalPrice":{"discountPrice":1999}},"promotions":null}]}}}})";
    // discountSetting is an object in the live feed; percentage 0 means free.
    CHECK(parse::epic_promotions(epicjson, 1795800000, epic, error)); // 2026-11-27
    CHECK(epic.size() == 1 && epic[0].title == "Free Now" && epic[0].page_url == "https://store.epicgames.com/p/free-now");
    std::vector<Game> expired;
    CHECK(parse::epic_promotions(epicjson, 1797000000, expired, error) && expired.empty()); // after the end date
}

static std::string make_zip(const std::string &path, const std::vector<std::pair<std::string, std::string>> &files)
{
    mz_zip_archive archive{};
    mz_zip_writer_init_file(&archive, path.c_str(), 0);
    for (const auto &[name, data] : files)
        mz_zip_writer_add_mem(&archive, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&archive);
    mz_zip_writer_end(&archive);
    return fs::read_file(path, 64 * 1024 * 1024).value_or("");
}

static const char *kParam = R"({"titleId":"PPSA77777","contentVersion":"01.000.003","applicationCategoryType":0,
  "localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Akeno Test Title"}}})";

static void test_zip()
{
    const std::string dir = temp_dir("zip");
    fs::make_dirs(dir);
    const std::string nested = fs::join(dir, "nested.zip");
    make_zip(nested, {{"Release/PPSA77777/eboot.bin", "SELF"},
                      {"Release/PPSA77777/sce_sys/param.json", kParam},
                      {"Release/PPSA77777/assets/data.bin", std::string(5000, 'x')},
                      {"README.txt", "outside"}});
    std::string error;
    const auto folder = zip::find_title_folder(nested, error);
    CHECK(folder && folder->prefix == "Release/PPSA77777/" && folder->files == 3);
    const std::string out = fs::join(dir, "out");
    CHECK(zip::extract(nested, folder->prefix, out, nullptr, error));
    CHECK(fs::is_file(fs::join(out, "eboot.bin")) && fs::file_size(fs::join(out, "assets/data.bin")) == 5000u);
    CHECK(!fs::exists(fs::join(out, "README.txt")));

    const std::string evil = fs::join(dir, "evil.zip");
    make_zip(evil, {{"../escape.txt", "x"}, {"PPSA1/eboot.bin", "x"}});
    CHECK(!zip::find_title_folder(evil, error));
    const std::string two = fs::join(dir, "two.zip");
    make_zip(two, {{"A/eboot.bin", "x"}, {"A/sce_sys/param.json", kParam}, {"B/eboot.bin", "x"},
                   {"B/sce_sys/param.json", kParam}});
    CHECK(!zip::find_title_folder(two, error));
    CHECK(!zip::safe_entry_name("a\\b") && !zip::safe_entry_name("/abs") && !zip::safe_entry_name("C:x") &&
          zip::safe_entry_name("a/b.c"));
    fs::remove_tree(fs::parent(dir));
}

// A transport that serves fixed bodies, for installer tests.
class FakeTransport final : public net::Transport
{
  public:
    std::map<std::string, std::string> bodies;
    std::map<std::string, std::string> redirects;
    const char *name() const override { return "fake"; }
    net::Response send(const net::Request &request, const net::Sink &sink, net::Cancel *) override
    {
        net::Response response;
        if (redirects.count(request.url))
        {
            response.status = 302;
            response.headers["location"] = redirects[request.url];
            return response;
        }
        const auto it = bodies.find(request.url);
        if (it == bodies.end())
        {
            response.status = 404;
            return response;
        }
        response.status = 200;
        for (std::size_t at = 0; at < it->second.size(); at += 1000)
        {
            const std::string_view chunk = std::string_view(it->second).substr(at, 1000);
            response.bytes += chunk.size();
            if (sink)
            {
                if (!sink(chunk, it->second.size()))
                {
                    response.error = "sink refused";
                    return response;
                }
            }
            else
                response.body.append(chunk);
        }
        return response;
    }
};

static bool wait_for(Installer &installer, int id, JobState &state)
{
    for (int i = 0; i < 400; ++i)
    {
        for (const Job &job : installer.jobs())
            if (job.id == id && job.finished())
            {
                state = job.state;
                if (job.state == JobState::Failed)
                    std::fprintf(stderr, "  job error: %s\n", job.error.c_str());
                return true;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void test_installer()
{
    const std::string dir = temp_dir("install");
    const std::string root = fs::join(dir, "data/homebrew");
    fs::make_dirs(root);
    const std::string zip_path = fs::join(dir, "PPSA77777.zip");
    const std::string zip = make_zip(zip_path, {{"PPSA77777/eboot.bin", "SELF"},
                                                {"PPSA77777/sce_sys/param.json", kParam},
                                                {"PPSA77777/sce_module/libc.prx", "PRX"}});
    auto transport = std::make_unique<FakeTransport>();
    transport->redirects["https://github.com/o/r/releases/download/v1/PPSA77777.zip"] =
        "https://release-assets.githubusercontent.com/blob";
    transport->bodies["https://release-assets.githubusercontent.com/blob"] = zip;
    net::Client client(std::move(transport));
    Library library(fs::join(dir, "receipts"));
    Installer installer(client, library, fs::join(dir, "receipts"), [](Game &, std::string &e) {
        e = "no resolver in this test";
        return false;
    });

    Game game;
    game.id = "prosperostore:PPSA77777";
    game.title = "Akeno Test Title";
    game.title_id = "PPSA77777";
    game.access = Access::Install;
    game.artifact_url = "https://github.com/o/r/releases/download/v1/PPSA77777.zip";
    game.sha256 = sha256_hex(zip);
    game.size = zip.size();
    game.resolved = true;
    JobState state{};
    CHECK(wait_for(installer, installer.install(game, root), state) && state == JobState::Done);
    CHECK(fs::is_file(fs::join(root, "PPSA77777/eboot.bin")));
    auto titles = library.scan({root});
    CHECK(titles.size() == 1 && titles[0].managed && titles[0].name == "Akeno Test Title" &&
          titles[0].content_version == "01.000.003");

    // A wrong hash leaves the installed version untouched.
    Game bad = game;
    bad.sha256 = std::string(64, '0');
    CHECK(wait_for(installer, installer.install(bad, root), state) && state == JobState::Failed);
    CHECK(fs::is_file(fs::join(root, "PPSA77777/sce_module/libc.prx")));

    // The archive's title ID must match the catalog's.
    Game other = game;
    other.id = "prosperostore:PPSA55555";
    other.title_id = "PPSA55555";
    CHECK(wait_for(installer, installer.install(other, root), state) && state == JobState::Failed);
    CHECK(!fs::exists(fs::join(root, "PPSA55555")));

    // Reinstall (update path) keeps the previous version aside.
    CHECK(wait_for(installer, installer.install(game, root), state) && state == JobState::Done);
    CHECK(fs::is_dir(fs::join(Installer::work_dir_for(root), "previous/PPSA77777")));

    titles = library.scan({root});
    CHECK(wait_for(installer, installer.uninstall(titles.at(0)), state) && state == JobState::Done);
    CHECK(!fs::exists(fs::join(root, "PPSA77777")) && library.scan({root}).empty());
    fs::remove_tree(fs::parent(dir));
}

static void test_merge()
{
    Game signed_entry;
    signed_entry.id = "prosperostore:PPSA99666";
    signed_entry.title_id = "PPSA99666";
    signed_entry.access = Access::Install;
    signed_entry.signed_catalog = true;
    Game recomp;
    recomp.id = "recompstore:PPSA99666";
    recomp.title_id = "PPSA99666";
    recomp.hero_url = "https://recompstore.com/h.jpg";
    recomp.needs = "DOOM WADs";
    recomp.access = Access::Install;
    Game web;
    web.id = "itch:x";
    const auto merged = Catalog::merge({{signed_entry}, {recomp}, {web}});
    CHECK(merged.size() == 2);
    CHECK(merged[0].signed_catalog && merged[0].hero_url == recomp.hero_url && merged[0].needs == "DOOM WADs");
}

static void test_runtime_detection()
{
    const std::string dir = temp_dir("rt");
    fs::make_dirs(dir);
    const auto write = [&](const std::string &name, const std::string &data) {
        fs::write_atomic(fs::join(dir, name), data);
        return fs::join(dir, name);
    };
    CHECK(detect_game(write("game.nes", std::string("NES\x1a", 4) + std::string(100, 0))).family == "retro");
    CHECK(detect_game(write("mario.z64", std::string("\x80\x37\x12\x40", 4) + std::string(100, 0))).family == "n64");
    std::string gc(0x40, '\0');
    gc[0x1C] = '\xC2';
    gc[0x1D] = '\x33';
    gc[0x1E] = '\x9F';
    gc[0x1F] = '\x3D';
    CHECK(detect_game(write("game.iso", gc)).family == "gamecube");
    std::string pe(0x100, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3C] = 0x80;
    pe.replace(0x80, 6, std::string("PE\0\0\x64\x86", 6));
    const Detection windows = detect_game(write("Game.exe", pe));
    CHECK(windows.family == "windows" && windows.format.find("x86-64") != std::string::npos);
    CHECK(detect_game(write("movie.swf", "FWS\x0a")).family == "flash");
    const Detection pck = detect_game(write("game.pck", "GDPC\x02\0\0\0"));
    CHECK(pck.family.empty() && pck.engine == "Godot" && !pck.advice.empty());
    fs::make_dirs(fs::join(dir, "mvgame/www/js"));
    fs::write_atomic(fs::join(dir, "mvgame/www/js/rpg_core.js"), "var RPGMAKER_NAME = 'MV';");
    CHECK(detect_game(fs::join(dir, "mvgame")).engine == "RPG Maker MV");
    CHECK(!runtimes_for("windows").empty() && runtimes_for("windows")[0]->title_id == std::string("PPSA99170"));
    CHECK(runtimes_for("n64").size() >= 2);
    CHECK(scan_local_games({dir}).size() >= 6);
    fs::remove_tree(fs::parent(dir));
}

static void test_shortcuts()
{
    const std::string dir = temp_dir("shortcut");
    const std::string assets = fs::join(dir, "assets"), root = fs::join(dir, "data/homebrew");
    fs::make_dirs(fs::join(assets, "tile"));
    fs::make_dirs(fs::join(assets, "payload"));
    fs::make_dirs(root);
    fs::write_atomic(fs::join(assets, "tile/eboot.bin"), "SELF-tile");
    fs::write_atomic(fs::join(assets, "tile/libc.prx"), "PRX");
    std::string payload = std::string(64, '\0') + kLaunchSlotMarker + std::string(32, '\0');
    fs::write_atomic(fs::join(assets, "payload/akeno-launch.elf"), payload);
    ::setenv("AKENO_ASSETS", assets.c_str(), 1);

    CHECK(patch_launch_slot(payload, "PPSA90500"));
    CHECK(payload.find("PPSA90500") == 64 && payload.find(kLaunchSlotMarker) == std::string::npos);
    std::string bad = payload;
    CHECK(!patch_launch_slot(bad, "PPSA90500")); // marker already replaced

    ShortcutSpec spec;
    spec.name = "Mein Spiel";
    spec.target = "PPSA99170";
    spec.arguments = {"path=Z:\\data\\akeno\\games\\Spiel\\Game.exe"};
    spec.icon.width = spec.icon.height = 4;
    spec.icon.rgba.assign(16, 0xFF8844CCu);
    std::string error;
    const std::string tile = create_shortcut(spec, root, fs::join(dir, "receipts"), error);
    CHECK(tile == "PPSA90500");
    const std::string folder = fs::join(root, tile);
    CHECK(fs::is_file(fs::join(folder, "eboot.bin")) && fs::is_file(fs::join(folder, "sce_module/libc.prx")) &&
          fs::is_file(fs::join(folder, "akeno-launch.elf")));
    ParamInfo info;
    CHECK(parse_param_json(fs::read_file(fs::join(folder, "sce_sys/param.json")).value_or(""), info));
    CHECK(info.title_id == tile && info.name == "Mein Spiel" && info.game);
    const auto param = fs::read_file(fs::join(folder, "sce_sys/param.json")).value_or("");
    CHECK(param.find("\"contentId\": \"UP9000-PPSA90500_00-AKENOSHORTCUT500\"") != std::string::npos);
    CHECK(fs::read_file(fs::join(folder, "forward.txt")).value_or("") ==
          "AKENO-LAUNCH 1\nPPSA99170\npath=Z:\\data\\akeno\\games\\Spiel\\Game.exe\n");
    ui::Pixels icon;
    CHECK(ui::decode_image(fs::read_file(fs::join(folder, "sce_sys/icon0.png")).value_or(""), 1024, 1024, icon));
    CHECK(icon.width == 512 && icon.height == 512);
    Library library(fs::join(dir, "receipts"));
    const auto titles = library.scan({root});
    CHECK(titles.size() == 1 && titles[0].shortcut && titles[0].managed);
    CHECK(create_shortcut(spec, root, fs::join(dir, "receipts"), error) == "PPSA90501");
    spec.target = "not-a-title";
    CHECK(create_shortcut(spec, root, fs::join(dir, "receipts"), error).empty());
    ::unsetenv("AKENO_ASSETS");
    fs::remove_tree(fs::parent(dir));
}

int main()
{
    platform::init();
    test_util();
    test_hash();
    test_prosperostore();
    test_other_stores();
    test_zip();
    test_installer();
    test_merge();
    test_runtime_detection();
    test_shortcuts();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
