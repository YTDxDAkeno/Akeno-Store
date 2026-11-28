// Akeno Gaming Hub - runtime components.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "play/components.hpp"

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/util.hpp"
#include "platform/platform.hpp"
#include "runtime/runtimes.hpp"

namespace akeno
{

const std::vector<ComponentSpec> &component_specs()
{
    // Each source is a title in the signed ProsperoStore catalog: its download is checked
    // against the signed SHA-256 before anything is kept.
    static const std::vector<ComponentSpec> specs = {
        {"wine", "Windows-Runtime (Wine aus Prospero Win)", "prosperostore:PPSA99995", "win/",
         "win/wine/lib/wine/x86_64-unix/ntdll.prx", "LGPL-2.1-or-later (Wine, Prospero Win)"},
    };
    return specs;
}

const ComponentSpec *component_spec(const std::string &id)
{
    for (const ComponentSpec &spec : component_specs())
        if (id == spec.id)
            return &spec;
    return nullptr;
}

std::string components_root() { return fs::join(platform::data_dir(), "runtimes"); }

std::string component_record_json(const InstalledComponent &c)
{
    return "{\n  \"schema\": 1,\n  \"id\": " + json::quote(c.id) + ",\n  \"version\": " + json::quote(c.version) +
           ",\n  \"path\": " + json::quote(c.path) + ",\n  \"source\": " + json::quote(c.source) +
           ",\n  \"sha256\": " + json::quote(c.sha256) + ",\n  \"installed\": " + std::to_string(c.installed) + "\n}\n";
}

bool parse_component_record(const std::string &text, InstalledComponent &out)
{
    const json::Document doc = json::Document::parse(text, 64 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || root["schema"].integer() != 1)
        return false;
    InstalledComponent c;
    c.id = root.str("id");
    c.version = sanitize_text(root.str("version"), 60);
    c.path = root.str("path");
    c.source = root.str("source");
    c.sha256 = root.str("sha256");
    c.installed = root["installed"].integer();
    if (!component_spec(c.id) || c.path.empty() || c.path.front() != '/' || c.path.find("/../") != std::string::npos)
        return false;
    out = std::move(c);
    return true;
}

std::optional<InstalledComponent> installed_component(const std::string &id)
{
    if (!component_spec(id))
        return std::nullopt;
    const auto text = fs::read_file(fs::join(fs::join(components_root(), id), "component.json"), 64 * 1024);
    InstalledComponent component;
    if (!text || !parse_component_record(*text, component) || component.id != id || !fs::is_dir(component.path))
        return std::nullopt;
    return component;
}

bool write_component_record(const InstalledComponent &component)
{
    const std::string dir = fs::join(components_root(), component.id);
    return fs::make_dirs(dir) && fs::write_atomic(fs::join(dir, "component.json"), component_record_json(component));
}

std::vector<std::string> required_components(const std::string &runtime)
{
    const RuntimeInfo *info = runtime_by_id(runtime);
    std::vector<std::string> out;
    if (info && info->components)
        for (const std::string &id : split(info->components, ','))
            if (!trim(id).empty())
                out.push_back(trim(id));
    return out;
}

std::vector<std::string> missing_components(const std::string &runtime)
{
    std::vector<std::string> out;
    for (const std::string &id : required_components(runtime))
        if (!installed_component(id))
            out.push_back(id);
    return out;
}

} // namespace akeno
