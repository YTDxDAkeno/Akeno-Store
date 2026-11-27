// Akeno Gaming Hub - GitHub release asset resolution shared by several providers.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stores/provider.hpp"

namespace akeno
{

// Shell-style match with '*' and '?' (no character classes).
bool glob_match(std::string_view pattern, std::string_view text);

// Reads the latest release of `repository` and fills artifact_url, size, sha256 and
// version for the first asset matching `pattern` (or the only title-ID-named ZIP).
// GitHub publishes a SHA-256 digest for each asset; without one nothing is installable.
bool resolve_github_latest(ProviderContext &context, const std::string &repository, const std::string &pattern,
                           Game &game, std::string &error);

} // namespace akeno
