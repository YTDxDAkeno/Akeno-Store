// Akeno Gaming Hub - GitHub releases: asset resolution and the repository provider.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "stores/github.hpp"

#include "core/hash.hpp"
#include "core/json.hpp"
#include "core/util.hpp"

#include <cctype>

namespace akeno
{

bool glob_match(std::string_view pattern, std::string_view text)
{
    std::size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            mark = t;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            t = ++mark;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

static bool valid_repository(const std::string &repository)
{
    const auto parts = split(repository, '/');
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty() || repository.size() > 140)
        return false;
    for (char c : repository)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '/'))
            return false;
    return parts[0] != "." && parts[0] != ".." && parts[1] != "." && parts[1] != "..";
}

namespace parse
{
bool github_release(std::string_view release, const std::string &wanted_asset, Game &game, std::string &error)
{
    const json::Document doc = json::Document::parse(release, 8 * 1024 * 1024);
    const json::Value root = doc.root();
    if (!root.is_object() || !root["assets"].is_array())
    {
        error = doc.ok() ? "GitHub returned no release" : doc.error();
        return false;
    }
    json::Value chosen;
    root["assets"].each([&](json::Value asset) {
        if (chosen.valid())
            return;
        const std::string name = asset.str("name");
        if (!wanted_asset.empty() ? glob_match(wanted_asset, name)
                                  : (ends_with(lower(name), ".zip") &&
                                     (game.title_id.empty() || name.find(game.title_id) != std::string::npos)))
            chosen = asset;
    });
    if (!chosen.valid())
    {
        error = "The latest release has no matching ZIP asset";
        return false;
    }
    const std::string digest = chosen.str("digest");
    if (!starts_with(digest, "sha256:") || digest.size() != 7 + 64)
    {
        error = "GitHub lists no SHA-256 for this asset, so it cannot be verified";
        return false;
    }
    const std::string sha = lower(digest.substr(7));
    if (!game.sha256.empty() && !digest_equal(game.sha256, sha))
    {
        error = "The store's SHA-256 differs from GitHub's digest";
        return false;
    }
    game.sha256 = sha;
    game.artifact_url = chosen.str("browser_download_url");
    game.asset_name = chosen.str("name");
    game.size = static_cast<std::uint64_t>(std::max<std::int64_t>(0, chosen["size"].integer()));
    game.format = "zip";
    const std::string tag = root.str("tag_name");
    if (!tag.empty())
        game.version = sanitize_text(tag, 60);
    if (game.release_notes.empty())
        game.release_notes = sanitize_text(root.str("body"), 4000);
    if (!is_https_url(game.artifact_url))
    {
        error = "The asset has no HTTPS download address";
        return false;
    }
    return true;
}
} // namespace parse

bool resolve_github_latest(ProviderContext &context, const std::string &repository, const std::string &pattern,
                           Game &game, std::string &error)
{
    if (!valid_repository(repository))
    {
        error = "Invalid GitHub repository";
        return false;
    }
    const net::Response response =
        context.http->get("https://api.github.com/repos/" + repository + "/releases/latest", 8 * 1024 * 1024,
                          nullptr, {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}});
    if (!response.ok())
    {
        error = response.error.empty() ? "GitHub answered HTTP " + std::to_string(response.status) : response.error;
        if (response.status == 403 || response.status == 429)
            error += " (rate limit; try again later)";
        return false;
    }
    if (!parse::github_release(response.body, pattern, game, error))
        return false;
    game.resolved = true;
    return true;
}

namespace
{

// Repositories the user adds in settings (owner/name), resolved from their latest release.
class GitHubProvider final : public Provider
{
  public:
    explicit GitHubProvider(std::vector<std::string> repositories) : repositories_(std::move(repositories)) {}
    const char *id() const override { return "github"; }
    const char *label() const override { return "GitHub"; }
    const char *about() const override
    {
        return "Eigene GitHub-Repositories mit PS5-Titel-ZIPs. Installiert wird nur ein Release-Asset "
               "mit von GitHub veröffentlichtem SHA-256.";
    }

    bool refresh(ProviderContext &context, std::vector<Game> &out, std::string &error) override
    {
        for (const std::string &repository : repositories_)
        {
            if (!valid_repository(repository))
                continue;
            Game game;
            game.provider = "github";
            game.provider_label = "GitHub";
            game.id = "github:" + lower(repository);
            game.source_repo = repository;
            game.title = repository.substr(repository.find('/') + 1);
            game.developer = repository.substr(0, repository.find('/'));
            game.page_url = "https://github.com/" + repository;
            game.runtime = "native";
            game.price = "Kostenlos";
            game.tags = {"ps5-native", "free"};
            game.access = Access::Install;
            std::string asset_error;
            const net::Response repo = context.http->get("https://api.github.com/repos/" + repository, 2 * 1024 * 1024,
                                                         nullptr, {{"Accept", "application/vnd.github+json"}});
            if (repo.ok())
            {
                const json::Document doc = json::Document::parse(repo.body);
                game.description = sanitize_text(doc.root().str("description"), 2000);
                game.license = doc.root()["license"].str("spdx_id");
                game.updated = doc.root().str("pushed_at");
            }
            else
                error = repo.error.empty() ? "GitHub answered HTTP " + std::to_string(repo.status) : repo.error;
            out.push_back(std::move(game));
        }
        return error.empty() || !out.empty();
    }

    bool resolve(ProviderContext &context, Game &game, std::string &error) override
    {
        return resolve_github_latest(context, game.source_repo, game.asset_name, game, error);
    }

  private:
    std::vector<std::string> repositories_;
};

} // namespace

std::unique_ptr<Provider> make_github_provider(std::vector<std::string> repositories)
{
    return std::make_unique<GitHubProvider>(std::move(repositories));
}

} // namespace akeno
