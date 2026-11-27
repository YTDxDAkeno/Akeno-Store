# Provider- und Runtime-Schnittstellen

## Provider

Provider stellen `list_games() -> list[dict]` bereit. Der Host registriert sie mit einem eindeutigen Anzeigenamen. Katalogeinträge haben mindestens `id` und `title`. `stores.ManifestProvider` validiert den Paketvertrag. ID-Kollisionen werden in Reihenfolge der Quellen aufgelöst. Die integrierte Offline-Quelle steht zuerst.

```sh
python3 -m stores manifests/example.catalog.json
python3 -m core.server --offline --manifest manifests/example.catalog.json
```

Für ein eigenes Paket verwende das Schema in `manifests/`, die Architektur `any`, eine existierende relative HTML-Startdatei und SHA-256 des fertigen ZIP. Stelle das ZIP nur über einen bestätigten HTTPS-Endpunkt bereit. Entwickler können der Runtime kompatible Metadaten hinzufügen, ohne spielspezifischen Startercode zu ändern.

## HTML5-Runtime

Die Runtime startet die im Manifest eingetragene Datei in einem opaque-origin Frame. Nutze relative Pfade und klassische Skripte. Für Richtungsnavigation akzeptiert Signal Drift Nachrichten im Format `{type: "akeno-direction", key: "ArrowLeft"}` aus `window.parent`. Weitere Spiele können denselben Vertrag implementieren. Die Manifestvalidierung lässt derzeit HTML5 zu. Ein natives SDK und ein Recompilation-Profilformat setzen eine validierte Ziel-Toolchain voraus.
