"""Exercise the actual Hub UI and bundled puzzle using a local Chromium browser."""

import os
import tempfile
import threading
from pathlib import Path

from playwright.sync_api import expect, sync_playwright

from core.server import HubServer
from core.service import Hub


def exercise(page, address):
    page.goto(address)
    page.locator("#hero-game").click()
    page.locator("[data-action=install]").click()
    expect(page.locator("[data-action=play]")).to_be_visible(timeout=12_000)
    with page.expect_popup() as popup:
        page.locator("[data-action=play]").click()

    player = popup.value
    player_errors = []
    player.on("pageerror", lambda error: player_errors.append(str(error)))
    game = player.frame_locator("iframe")
    expect(game.locator("#status")).to_have_text("Relais 0/3 · Energie 24")
    game.locator("[data-move=ArrowRight]").click()
    expect(game.locator("#board")).to_have_attribute("data-position", "1")

    keys = {"R": "ArrowRight", "D": "ArrowDown", "L": "ArrowLeft", "U": "ArrowUp"}
    route = "RRRLDDDLLLRRDRRR"
    for direction in route:
        player.keyboard.press(keys[direction])
    expect(game.locator("#status")).to_have_text("Signal verbunden. Du hast gewonnen!")
    assert not player_errors, player_errors
    player.close()

    page.locator("[data-action=remove]").click()
    page.locator("[data-action=confirm]").click()
    expect(page.locator("[data-action=install]")).to_be_visible(timeout=12_000)


def main():
    with tempfile.TemporaryDirectory() as temporary:
        hub = Hub(Path(temporary), [])
        with HubServer(0, hub) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with sync_playwright() as playwright:
                    browser = playwright.chromium.launch(executable_path=os.environ.get("CHROMIUM"))
                    page = browser.new_page(viewport={"width": 1440, "height": 900})
                    errors = []
                    page.on("pageerror", lambda error: errors.append(str(error)))
                    exercise(page, f"http://127.0.0.1:{server.server_address[1]}")
                    assert not errors, errors
                    browser.close()
            finally:
                server.shutdown()
                thread.join()
                hub.close()
    print("Chromium: installiert, gestartet, Rätsel gelöst und deinstalliert.")


if __name__ == "__main__":
    main()
