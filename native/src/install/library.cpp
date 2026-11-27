// Akeno Gaming Hub - installed PS5 titles in the folders ShadowMountPlus scans.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/library.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <ctime>

namespace akeno
{

bool parse_param_json(const std::string &text, ParamInfo &out)
{
    const json::Document doc = json::Document::parse(text, 256 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object())
        return false;
    out.title_id = root.str("titleId");
    if (!is_title_id(out.title_id))
        return false;
    out.content_version = root.str("contentVersion");
    out.game = root["applicationCategoryType"].integer(0) == 0;
    const json::Value localized = root["localizedParameters"];
    const std::string language = localized.str("defaultLanguage", "en-US");
    out.name = sanitize_text(localized[language.c_str()].str("titleName"), 160);
    if (out.name.empty())
        out.name = sanitize_text(localized["en-US"].str("titleName"), 160);
    if (out.name.empty())
        out.name = out.title_id;
    return true;
}

std::vector<InstalledTitle> Library::scan(const std::vector<std::string> &roots) const
{
    std::vector<InstalledTitle> titles;
    const auto visit = [&](const std::string &root, const std::string &folder) {
        const auto param = fs::read_file(fs::join(folder, "sce_sys/param.json"), 256 * 1024);
        if (!param || !fs::is_file(fs::join(folder, "eboot.bin")))
            return false;
        ParamInfo info;
        if (!parse_param_json(*param, info))
            return false;
        InstalledTitle title;
        title.title_id = info.title_id;
        title.name = info.name;
        title.content_version = info.content_version;
        title.folder = folder;
        title.root = root;
        title.game = info.game;
        const std::string icon = fs::join(folder, "sce_sys/icon0.png");
        if (fs::is_file(icon))
            title.icon_path = icon;
        if (const auto receipt = fs::read_file(fs::join(receipts_, info.title_id + ".json"), 64 * 1024))
        {
            const json::Document doc = json::Document::parse(*receipt);
            if (doc.root().str("folder") == folder)
            {
                title.managed = true;
                title.source_game_id = doc.root().str("game");
                title.shortcut = doc.root()["shortcut"].boolean(false);
            }
        }
        titles.push_back(std::move(title));
        return true;
    };
    for (const std::string &root : roots)
        for (const fs::Entry &entry : fs::list(root))
        {
            if (!entry.directory || entry.name.empty() || entry.name[0] == '.')
                continue;
            const std::string folder = fs::join(root, entry.name);
            if (visit(root, folder))
                continue;
            for (const fs::Entry &inner : fs::list(folder)) // one nested level, as ShadowMountPlus allows
                if (inner.directory && inner.name[0] != '.')
                    visit(root, fs::join(folder, inner.name));
        }
    std::sort(titles.begin(), titles.end(), [](const InstalledTitle &a, const InstalledTitle &b) {
        return lower(a.name) < lower(b.name);
    });
    return titles;
}

void Library::write_receipt(const std::string &title_id, const std::string &game_id, const std::string &version,
                            const std::string &sha256, const std::string &folder, bool shortcut) const
{
    fs::make_dirs(receipts_);
    std::string text = "{\"schema\":1,\"title_id\":" + json::quote(title_id) + ",\"game\":" + json::quote(game_id) +
                       ",\"version\":" + json::quote(version) + ",\"sha256\":" + json::quote(sha256) +
                       ",\"folder\":" + json::quote(folder) + ",\"shortcut\":" + (shortcut ? "true" : "false") +
                       ",\"installed\":" + std::to_string(static_cast<long long>(std::time(nullptr))) + "}\n";
    fs::write_atomic(fs::join(receipts_, title_id + ".json"), text);
}

void Library::remove_receipt(const std::string &title_id) const
{
    fs::remove_tree(fs::join(receipts_, title_id + ".json"));
}

bool title_running(const std::string &title_id)
{
    if (!platform::is_console())
        return false;
    for (const fs::Entry &entry : fs::list("/mnt/sandbox"))
        if (starts_with(entry.name, title_id + "_"))
            return true;
    return false;
}

} // namespace akeno
