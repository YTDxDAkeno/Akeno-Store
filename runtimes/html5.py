"""Browser runtime for local, self-contained games."""

from html import escape
from urllib.parse import quote

RUNTIME = {
    "id": "html5",
    "name": "HTML5",
    "version": "1",
    "state": "available",
    "description": "Lokale Browser-Spiele in einem isolierten Frame. Netzwerkzugriff ist gesperrt.",
}
def game_policy(host: str, game_id: str) -> str:
    # In a sandbox the document has an opaque origin. Point resource directives
    # at this game's URL prefix instead of relying on the meaning of 'self'.
    assets = f"http://{host}/games/{quote(game_id, safe='')}/"
    return (
        "sandbox allow-scripts; default-src 'none'; "
        f"script-src {assets} 'unsafe-inline'; style-src {assets} 'unsafe-inline'; "
        f"img-src {assets} data:; media-src {assets} blob:; font-src {assets}; "
        f"base-uri 'none'; form-action 'none'; frame-ancestors http://{host}"
    )


def player(title: str, game_id: str, entrypoint: str) -> bytes:
    src = f"/games/{quote(game_id, safe='')}/{quote(entrypoint)}"
    return f"""<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escape(title)} · Akeno</title>
<style>
body{{margin:0;background:#0c0e1b;color:#f3f4ff;font:16px system-ui;display:grid;grid-template-rows:auto 1fr;height:100vh}}
header{{display:flex;justify-content:space-between;align-items:center;padding:.8rem 1.25rem;gap:1rem}}
a{{color:#bba9ff}} iframe{{width:100%;height:100%;border:0}}
</style>
<script src="/runtime/player.js" defer></script>
</head>
<body>
<header><strong>{escape(title)}</strong><a href="/">Zurück zum Hub</a></header>
<iframe title="{escape(title)}" src="{escape(src, quote=True)}" sandbox="allow-scripts"></iframe>
</body>
</html>""".encode()


def supports(game: dict) -> bool:
    return (
        game.get("runtime") == RUNTIME["id"]
        and game.get("architecture") in {"any"}
        and isinstance(game.get("entrypoint"), str)
        and game["entrypoint"].lower().endswith((".html", ".htm"))
    )
