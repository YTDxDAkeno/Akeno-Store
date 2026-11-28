// Akeno Gaming Hub - runtime components: large runtime files Akeno downloads once and every
// game of that runtime shares (Wine, a JVM, a web engine, a game client ...).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A component comes from a catalog entry Akeno already verifies, normally a PS5 title ZIP
// in the signed ProsperoStore catalog (its SHA-256 is signed). Akeno keeps only the
// subtree the runtime needs, under <data>/runtimes/<id>/<version>/, and records it in
// <data>/runtimes/<id>/component.json. Nothing is installed as a separate application.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace akeno
{

struct ComponentSpec
{
    const char *id;      // wine, jvm, webkit, ...
    const char *name;    // shown in downloads and the runtime manager
    const char *source;  // catalog id of the entry that carries the files ("prosperostore:PPSA99995")
    const char *keep;    // subtree inside the title folder to keep ("win/"), "" for all of it
    const char *marker;  // a file that must exist below `keep` afterwards (proves the layout)
    const char *license; // licence of what is kept
};

const std::vector<ComponentSpec> &component_specs();
const ComponentSpec *component_spec(const std::string &id);

struct InstalledComponent
{
    std::string id, version, path, source, sha256;
    std::int64_t installed = 0;
};

std::string components_root();                            // <data>/runtimes
std::optional<InstalledComponent> installed_component(const std::string &id);
bool write_component_record(const InstalledComponent &component);
std::string component_record_json(const InstalledComponent &component);
bool parse_component_record(const std::string &text, InstalledComponent &out);

// The components a runtime needs before it can play (from RuntimeInfo::components).
std::vector<std::string> required_components(const std::string &runtime);
// Those of them that are not installed yet.
std::vector<std::string> missing_components(const std::string &runtime);

} // namespace akeno
