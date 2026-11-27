"""Optional tests that use the same public network endpoints as production."""

import os
import unittest


from stores import (
    GITHUB_PC_PROJECTS,
    GITHUB_PS5_PROJECTS,
    GitHubProvider,
    ItchProvider,
    ProsperoStoreProvider,
)

LIVE = frozenset(
    provider.strip() for provider in os.environ.get("AKENO_LIVE_STORES", "").split(",")
)


class LiveStoresTest(unittest.TestCase):
    @unittest.skipUnless("github" in LIVE, "set AKENO_LIVE_STORES=github to query GitHub")
    def test_github_published_release_feeds(self):
        games = GitHubProvider(GITHUB_PC_PROJECTS + GITHUB_PS5_PROJECTS).list_games()
        self.assertEqual(
            {game["repository"] for game in games},
            {project.repository for project in GITHUB_PC_PROJECTS + GITHUB_PS5_PROJECTS},
        )
        self.assertTrue(all(game["version"] and game["updated_at"] for game in games))
        self.assertTrue(all("archive_url" not in game for game in games))

    @unittest.skipUnless("itch" in LIVE, "set AKENO_LIVE_STORES=itch to query itch.io")
    def test_itch_public_free_games_feed(self):
        games = ItchProvider().list_games()
        self.assertTrue(games, "The live free games feed yielded no visible titles")
        self.assertTrue(all(game["price_kind"] == "listed-free" for game in games))
        self.assertTrue(all(game["source_url"].startswith("https://") for game in games))
        self.assertTrue(all("cover_url" not in game and "archive_url" not in game for game in games))

    @unittest.skipUnless("prosperostore" in LIVE, "set AKENO_LIVE_STORES=prosperostore")
    def test_prosperostore_published_game_records(self):
        games = ProsperoStoreProvider().list_games()
        self.assertGreater(len(games), 200)
        self.assertIn("PPSA99874", {game["title_id"] for game in games})