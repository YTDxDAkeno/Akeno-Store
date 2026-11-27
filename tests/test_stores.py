import json
import socket
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from stores import CatalogError, GitHubProject, GitHubProvider, ItchProvider, ManifestProvider
from stores import ProsperoStoreProvider


ATOM = b'''<feed xmlns="http://www.w3.org/2005/Atom">
 <entry>
  <title>v2.4</title>
  <updated>2026-03-15T10:37:12+01:00</updated>
  <link rel="alternate" href="https://github.com/example/jump/releases/tag/v2.4"/>
  <content type="html">&lt;script&gt;untrusted()&lt;/script&gt;</content>
 </entry>
 <entry><title>v2.3</title></entry>
</feed>'''
RSS = b'''<rss version="2.0"><channel>
 <item>
  <title>Sky Jump</title>
  <link>https://example.itch.io/sky-jump?tracking=1</link>
  <description>&lt;img src="https://example.org/a.png"&gt;</description>
  <category>Platformer</category>
  <pubDate>Sun, 15 Mar 2026 13:01:07 +0100</pubDate>
 </item>
 <item>
  <title>Restricted</title>
  <link>https://example.itch.io/restricted</link>
  <category>NSFW</category>
 </item>
 <item>
  <title>Impostor</title>
  <link>https://example.itch.io.example.org/sky-jump</link>
 </item>
</channel></rss>'''


class StoresTest(unittest.TestCase):
    def test_github_reads_latest_release_without_treating_it_as_installable(self):
        with patch("stores.providers._fetch", return_value=ATOM) as fetch:
            games = GitHubProvider(["Example/Jump", "example/jump"]).list_games()

        fetch.assert_called_once_with(
            "https://github.com/example/jump/releases.atom", "application/atom+xml"
        )
        self.assertEqual(len(games), 1)
        game = games[0]
        self.assertEqual(game["provider"], "github")
        self.assertEqual(game["version"], "v2.4")
        self.assertEqual(game["updated_at"], "2026-03-15T09:37:12Z")
        self.assertEqual(game["source_url"], "https://github.com/example/jump/releases/tag/v2.4")
        self.assertNotIn("archive_url", game)
        self.assertNotIn("description", game)

    def test_github_curated_project_has_platform_and_scoped_license(self):
        project = GitHubProject("example/jump", "Jump", "Arcade", "A test release", "MIT (code)", "ps5")
        with patch("stores.providers._fetch", return_value=ATOM.replace(b"v2.4</title>", b"Jump 2.4</title>")):
            game, = GitHubProvider([project]).list_games()

        self.assertEqual(game["title"], "Jump")
        self.assertEqual(game["version"], "v2.4")
        self.assertEqual(game["release_title"], "Jump 2.4")
        self.assertEqual(game["target_platform"], "ps5")
        self.assertEqual(game["license"], "MIT (code)")
        self.assertNotIn("entrypoint", game)

    def test_github_rejects_repository_path_manipulation(self):
        with self.assertRaises(CatalogError):
            GitHubProvider(["example/name/../../different"])

    def test_itch_omits_restricted_items_and_external_media(self):
        with patch("stores.providers._fetch", return_value=RSS):
            games = ItchProvider().list_games()

        self.assertEqual(len(games), 1)
        game = games[0]
        self.assertEqual(game["title"], "Sky Jump")
        self.assertEqual(game["source_url"], "https://example.itch.io/sky-jump")
        self.assertEqual(game["age_rating"], "unrated")
        self.assertEqual(game["price_kind"], "listed-free")
        self.assertEqual(game["categories"], ["Platformer"])
        self.assertEqual(game["published_at"], "2026-03-15T12:01:07Z")
        self.assertNotIn("description", game)
        self.assertNotIn("archive_url", game)

    def test_itch_does_not_parse_dtds(self):
        with patch(
            "stores.providers._fetch",
            return_value=b'<!DOCTYPE rss [<!ENTITY bad "ignored">]><rss><channel/></rss>',
        ):
            with self.assertRaises(CatalogError):
                ItchProvider().list_games()


class ManifestTest(unittest.TestCase):
    def setUp(self):
        workspace = tempfile.TemporaryDirectory()
        self.addCleanup(workspace.cleanup)
        self.path = Path(workspace.name) / "catalog.json"
        self.game = {
            "id": "sky-jump",
            "title": "Sky Jump",
            "version": "2.4",
            "runtime": "html5",
            "entrypoint": "site/index.html",
            "archive_url": "https://example.org/games/sky.zip",
            "sha256": "A" * 64,
            "license": "MIT",
            "architecture": "any",
            "age_rating": "all",
        }

    def write_catalog(self, games):
        self.path.write_text(json.dumps({"schema_version": 1, "games": games}), encoding="utf-8")

    def test_manifest_yields_validated_installable_games(self):
        self.write_catalog([self.game])
        self.assertEqual(
            ManifestProvider(self.path).list_games(),
            [{**self.game, "provider": "manifest", "sha256": "a" * 64}],
        )

    def test_manifest_omits_restricted_titles(self):
        self.write_catalog([{**self.game, "age_rating": "restricted"}])
        self.assertEqual(ManifestProvider(self.path).list_games(), [])

    def test_manifest_limits_artwork_to_rated_entries(self):
        art = {
            "cover_url": "https://example.org/cover.webp",
            "screenshots": ["https://example.org/scene.png"],
        }
        self.write_catalog([{**self.game, **art}])
        game, = ManifestProvider(self.path).list_games()
        self.assertEqual({field: game[field] for field in art}, art)

        self.write_catalog([{**self.game, **art, "age_rating": "unrated"}])
        game, = ManifestProvider(self.path).list_games()
        self.assertNotIn("cover_url", game)
        self.assertNotIn("screenshots", game)

    def test_manifest_rejects_duplicate_ids(self):
        self.write_catalog([self.game, self.game])
        with self.assertRaisesRegex(CatalogError, "Duplicate game ID"):
            ManifestProvider(self.path).list_games()

    def test_manifest_rejects_duplicate_fields(self):
        self.path.write_text('{"schema_version":1,"games":[],"games":[]}', encoding="utf-8")
        with self.assertRaisesRegex(CatalogError, "Duplicate JSON key"):
            ManifestProvider(self.path).list_games()

    def test_manifest_rejects_unsafe_or_incomplete_install_metadata(self):
        for replacement in (
            {"entrypoint": "../index.html"},
            {"entrypoint": "C:/index.html"},
            {"archive_url": "http://example.org/sky.zip"},
            {"archive_url": "https://127.0.0.1/sky.zip"},
            {"archive_url": "https://example.org@othersite.org/sky.zip"},
            {"sha256": "broken"},
        ):
            with self.subTest(replacement=replacement):
                self.write_catalog([{**self.game, **replacement}])
                with self.assertRaises(CatalogError):
                    ManifestProvider(self.path).list_games()

    def test_remote_manifest_rejects_non_public_resolution(self):
        provider = ManifestProvider.from_url("https://example.org/catalog.json")
        result = [(socket.AF_INET, socket.SOCK_STREAM, 6, "", ("192.168.1.1", 443))]
        with patch("stores.providers.socket.getaddrinfo", return_value=result):
            with patch("stores.providers._fetch") as fetch:
                with self.assertRaisesRegex(CatalogError, "public addresses"):
                    provider.list_games()
                fetch.assert_not_called()


class ProsperoStoreTest(unittest.TestCase):
    def setUp(self):
        self.project = GitHubProject(
            "example/jump", "Jump", "Arcade", "A test release", "MIT (code)", "ps5", "all",
        )
        self.record = {
            "kind": "game", "titleid": "PPSA99999", "version": "01.000.001",
            "source_repo": "https://github.com/example/jump",
            "artifact_url": "https://github.com/example/jump/releases/download/v1/jump.zip",
            "sha256": "1" * 64,
            "icon_url": "https://raw.githubusercontent.com/example/jump/v1/sce_sys/icon0.png",
        }
        projects = patch("stores.prospero.PROJECTS", (("PPSA99999", self.project),))
        projects.start()
        self.addCleanup(projects.stop)

    def test_known_game_records_only_expose_discovery_metadata(self):
        with patch("stores.prospero._fetch", return_value=json.dumps(self.record).encode()) as fetch:
            game, = ProsperoStoreProvider().list_games()

        fetch.assert_called_once_with(
            "https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/main/apps/PPSA99999.json",
            "application/json",
        )
        self.assertEqual(game["version"], "01.000.001")
        self.assertEqual(game["provider"], "prosperostore")
        self.assertEqual(game["target_platform"], "ps5")
        self.assertNotIn("artifact_url", game)
        self.assertNotIn("sha256", game)
        self.assertEqual(game["cover_url"], self.record["icon_url"])

    def test_record_repository_must_match_reviewed_source(self):
        self.record["source_repo"] = "https://github.com/different/repository"
        with patch("stores.prospero._fetch", return_value=json.dumps(self.record).encode()):
            with self.assertRaisesRegex(CatalogError, "changed identity"):
                ProsperoStoreProvider().list_games()


if __name__ == "__main__":
    unittest.main()
