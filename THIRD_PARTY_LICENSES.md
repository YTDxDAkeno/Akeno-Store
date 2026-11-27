# Drittanbieter und Lizenzen

Das Host-Paket benötigt die Python-Standardbibliothek und einen Browser, die separat bereitgestellt werden. Es bündelt keine der in der Recherche genannten Engines, SDKs und Spiele. GitHub und itch.io dienen ausschließlich als Metadatenquellen.

Signal Drift ist in diesem Repository entwickelt. Spielcode und Spielressourcen stehen unter der [MIT-Lizenz](licenses/SIGNAL-DRIFT.txt). Die Hub-Grafik ist für dieses Projekt erstellt worden. Die individuellen Lizenzen externer Spielpakete gelten gemäß dem jeweiligen geprüften Katalogeintrag.

Der PS5-Zielmodul-Build verlinkt SDL2, libiconv, libsamplerate und die libc des PacBrew-Zielbaums. Die SDK-Systemimporte sind als dynamische Abhängigkeiten referenziert. Für eine binäre Veröffentlichung sind die Lizenz- und Quellcodepflichten der tatsächlich eingebundenen Komponenten einschließlich statischer Bibliotheken zu prüfen. Die Setup-Pipeline lädt das exakt gepinnte Upstream-Archiv. [PacBrew v0.40.2](https://github.com/ps5-payload-dev/pacbrew-repo/releases/tag/v0.40.2)
