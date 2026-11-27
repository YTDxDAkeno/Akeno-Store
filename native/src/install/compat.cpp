// Akeno Gaming Hub - the local compatibility database.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/compat.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"

#include <algorithm>

namespace akeno
{

namespace
{

const char *aspect_key(Aspect aspect)
{
    switch (aspect)
    {
    case Aspect::Works:
        return "ok";
    case Aspect::Issues:
        return "issues";
    case Aspect::Fails:
        return "fails";
    case Aspect::Unknown:
        break;
    }
    return "unknown";
}

Aspect aspect_from(const std::string &key)
{
    if (key == "ok")
        return Aspect::Works;
    if (key == "issues")
        return Aspect::Issues;
    if (key == "fails")
        return Aspect::Fails;
    return Aspect::Unknown;
}

bool read_report(json::Value value, CompatReport &report)
{
    if (!value.is_object())
        return false;
    report.game_id = sanitize_text(value.str("game"), 200);
    if (report.game_id.empty() || report.game_id.find(':') == std::string::npos)
        return false;
    report.title = sanitize_text(value.str("title"), 160);
    report.provider = sanitize_text(value.str("provider"), 40);
    report.game_version = sanitize_text(value.str("game_version"), 60);
    report.runtime = sanitize_text(value.str("runtime"), 60);
    report.runtime_version = sanitize_text(value.str("runtime_version"), 60);
    report.firmware = sanitize_text(value.str("firmware"), 30);
    report.status = compat_from_key(value.str("status"));
    const json::Value aspects = value["aspects"];
    report.install = aspect_from(aspects.str("install"));
    report.start = aspect_from(aspects.str("start"));
    report.gameplay = aspect_from(aspects.str("gameplay"));
    report.graphics = aspect_from(aspects.str("graphics"));
    report.audio = aspect_from(aspects.str("audio"));
    report.controller = aspect_from(aspects.str("controller"));
    report.network = aspect_from(aspects.str("network"));
    report.issues = sanitize_text(value.str("issues"), 1000);
    report.settings = sanitize_text(value.str("settings"), 1000);
    report.tested = value["tested"].integer();
    return true;
}

std::string write_report(const CompatReport &r)
{
    return "{\"game\":" + json::quote(r.game_id) + ",\"title\":" + json::quote(r.title) +
           ",\"provider\":" + json::quote(r.provider) + ",\"game_version\":" + json::quote(r.game_version) +
           ",\"runtime\":" + json::quote(r.runtime) + ",\"runtime_version\":" + json::quote(r.runtime_version) +
           ",\"firmware\":" + json::quote(r.firmware) + ",\"status\":" + json::quote(compat_key(r.status)) +
           ",\"aspects\":{\"install\":\"" + aspect_key(r.install) + "\",\"start\":\"" + aspect_key(r.start) +
           "\",\"gameplay\":\"" + aspect_key(r.gameplay) + "\",\"graphics\":\"" + aspect_key(r.graphics) +
           "\",\"audio\":\"" + aspect_key(r.audio) + "\",\"controller\":\"" + aspect_key(r.controller) +
           "\",\"network\":\"" + aspect_key(r.network) + "\"},\"issues\":" + json::quote(r.issues) +
           ",\"settings\":" + json::quote(r.settings) + ",\"tested\":" + std::to_string(r.tested) + "}";
}

} // namespace

const char *aspect_label(Aspect aspect)
{
    switch (aspect)
    {
    case Aspect::Works:
        return "funktioniert";
    case Aspect::Issues:
        return "mit Problemen";
    case Aspect::Fails:
        return "geht nicht";
    case Aspect::Unknown:
        break;
    }
    return "nicht geprüft";
}

Aspect next_aspect(Aspect aspect)
{
    switch (aspect)
    {
    case Aspect::Unknown:
        return Aspect::Works;
    case Aspect::Works:
        return Aspect::Issues;
    case Aspect::Issues:
        return Aspect::Fails;
    case Aspect::Fails:
        break;
    }
    return Aspect::Unknown;
}

const CompatReport *CompatDb::find(const std::string &game_id) const
{
    for (const CompatReport &report : reports_)
        if (report.game_id == game_id)
            return &report;
    return nullptr;
}

void CompatDb::put(const CompatReport &report)
{
    for (CompatReport &existing : reports_)
        if (existing.game_id == report.game_id)
        {
            existing = report;
            save();
            return;
        }
    reports_.push_back(report);
    save();
}

void CompatDb::remove(const std::string &game_id)
{
    reports_.erase(std::remove_if(reports_.begin(), reports_.end(),
                                  [&](const CompatReport &r) { return r.game_id == game_id; }),
                   reports_.end());
    save();
}

void CompatDb::set_pending(const PendingLaunch &launch)
{
    pending_ = launch;
    save();
}

void CompatDb::clear_pending()
{
    pending_.reset();
    save();
}

std::string CompatDb::export_json() const
{
    std::string text = "{\"schema\":1,\"kind\":\"akeno-compat\",\"reports\":[";
    for (std::size_t i = 0; i < reports_.size(); ++i)
        text += (i ? ",\n" : "\n") + write_report(reports_[i]);
    return text + "\n]}\n";
}

int CompatDb::import_json(const std::string &text, std::string &error)
{
    const json::Document doc = json::Document::parse(text, 8 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root.str("kind") != "akeno-compat" || !root["reports"].is_array())
    {
        error = doc.ok() ? "Die Datei ist kein Akeno-Kompatibilitätsexport" : doc.error();
        return -1;
    }
    int merged = 0;
    root["reports"].each([&](json::Value value) {
        CompatReport report;
        if (!read_report(value, report) || reports_.size() > 5000)
            return;
        const CompatReport *existing = find(report.game_id);
        if (existing && existing->tested >= report.tested)
            return;
        if (existing)
            for (CompatReport &r : reports_)
                if (r.game_id == report.game_id)
                    r = report;
        if (!existing)
            reports_.push_back(report);
        ++merged;
    });
    save();
    return merged;
}

bool CompatDb::load()
{
    const auto text = fs::read_file(path_, 8 * 1024 * 1024);
    if (!text)
        return false;
    const json::Document doc = json::Document::parse(*text, 8 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object())
        return false;
    root["reports"].each([&](json::Value value) {
        CompatReport report;
        if (read_report(value, report) && !find(report.game_id))
            reports_.push_back(report);
    });
    const json::Value pending = root["pending"];
    if (pending.is_object() && !pending.str("game").empty())
        pending_ = PendingLaunch{sanitize_text(pending.str("game"), 200), sanitize_text(pending.str("title"), 160),
                                 sanitize_text(pending.str("runtime"), 60), pending["started"].integer()};
    return true;
}

bool CompatDb::save() const
{
    std::string text = export_json();
    text.resize(text.size() - 3); // drop the closing "]}\n" to append the pending launch
    text += "],\"pending\":";
    text += pending_ ? "{\"game\":" + json::quote(pending_->game_id) + ",\"title\":" + json::quote(pending_->title) +
                           ",\"runtime\":" + json::quote(pending_->runtime) +
                           ",\"started\":" + std::to_string(pending_->started) + "}"
                     : std::string("null");
    text += "}\n";
    fs::make_dirs(fs::parent(path_));
    return fs::write_atomic(path_, text);
}

} // namespace akeno
