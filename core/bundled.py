"""Build the authored offline test package reproducibly from checked-in files."""

from hashlib import sha256
from io import BytesIO
from pathlib import Path
from tempfile import NamedTemporaryFile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

PROJECT = Path(__file__).resolve().parents[1]


def bundle(directory: Path) -> tuple[dict, Path]:
    files = PROJECT / "assets" / "signal-drift"
    buffer = BytesIO()
    with ZipFile(buffer, mode="w", compression=ZIP_DEFLATED) as output:
        for path in sorted(files.iterdir()):
            if path.is_file():
                info = ZipInfo(path.name)
                info.external_attr = 0o100644 << 16
                output.writestr(info, path.read_bytes(), compress_type=ZIP_DEFLATED)
    data = buffer.getvalue()
    digest = sha256(data).hexdigest()
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / f"{digest}.zip"
    if not destination.exists() or sha256(destination.read_bytes()).hexdigest() != digest:
        with NamedTemporaryFile(dir=directory, delete=False) as temporary:
            temporary.write(data)
            staged = Path(temporary.name)
        try:
            staged.replace(destination)
        finally:
            staged.unlink(missing_ok=True)
    return {
        "id": "signal-drift",
        "title": "Signal Drift",
        "version": "1.0.0",
        "provider": "Akeno",
        "runtime": "html5",
        "architecture": "any",
        "entrypoint": "index.html",
        "archive_url": "https://bundled.invalid/signal-drift.zip",
        "sha256": digest,
        "license": "MIT",
        "genre": "Puzzle",
        "modes": ["singleplayer"],
        "description": "Führe dein Signal über ein Raster, sammle drei Relais und erreiche das Ziel mit der verbleibenden Energie.",
        "featured": True,
        "download_size": len(data),
    }, destination
