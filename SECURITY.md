# Sicherheit

Der Host-Dienst bindet sich ausschließlich an IPv4-Loopback. Anfragen prüfen Host und, falls vorhanden, Origin. Schreibende Aufrufe akzeptieren JSON. Spiel-HTML läuft unter einer Sandbox mit Skriptausführung und ohne Same-Origin-Berechtigung. Die Inhaltsrichtlinie begrenzt Ressourcen und sperrt Fetch-/Socket-Zugriffe. Die Anwendung führt keine fremden Betriebssystemprozesse aus.

Das Paketformat ist ZIP. SHA-256 prüft vor der Extraktion gegen das Betreiber-Manifest. Die Extraktion begrenzt Größe und Einträge und weist Linkdateien und Pfade außerhalb des Installationsverzeichnisses ab. Der Installer prüft Ziele und Weiterleitungen und erhält die aktive Version bei fehlgeschlagenem Ersatz. Er kann nur die Integrität gegenüber einem vertrauten Manifest bestätigen. Installiere daher nur Quellen, deren Herausgeber und Verbreitungsrecht du geprüft hast.

Die Fehlerbehandlung ersetzt keine Prozess- und Ressourcenisolation. Browser-Speicher, beliebige Spielprozesse, OAuth, signierte Update-Manifeste und der Schutz des lokalen Prozessbenutzers gegen manipulierte Dateien sind außerhalb des Host-Funktionsumfangs. Ein sensibler oder unbekannter Katalog gehört nicht in die Standardquellen.
