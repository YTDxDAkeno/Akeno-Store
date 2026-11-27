# Schnittstellen für Erweiterungen

## Store-Provider (native App)

```cpp
class Provider {
    virtual const char *id() const;      // "itch", "gog", ...
    virtual const char *label() const;   // Anzeigename
    virtual const char *about() const;   // Vertrauensmodell in einem Satz
    virtual bool refresh(ProviderContext &, std::vector<Game> &, std::string &error);
    virtual bool resolve(ProviderContext &, Game &, std::string &error); // Download-URL + SHA-256
};
```

Ein `Game` trägt eine `Access`-Art (`Install`, `Runtime`, `External`, `Upcoming`). Die Oberfläche bietet
nur Aktionen an, die zur Art passen. Neue Provider werden in `ui/app.cpp` registriert und erhalten
Parser-Tests in `native/tests/core_tests.cpp`.

## Runtime-Titel

`runtime_registry()` in `native/src/runtime/runtimes.cpp` ordnet Formatfamilien den Title-IDs von
PS5-Runtime-Titeln zu. Neue Formate brauchen eine Signatur-Erkennung in `detect_file()` und einen Test.
Startargumente nur eintragen, wenn das Runtime-Projekt sie dokumentiert (`LaunchArgs`).

## Eigene Kataloge (Python-Host-Hub)

`manifests/catalog.schema.json` beschreibt versionierte HTML5-Kataloge für den Web-Hub
(`python3 -m stores manifests/example.catalog.json`). Für PS5-Titel ist der ProsperoStore-Katalogvertrag
der empfohlene Weg: Ein Fork von ps5-homebrew-catalog veröffentlicht dieselbe API.
