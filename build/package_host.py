"""Create a deterministic source distribution for a Python 3.12+ host."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = (
    "apps", "assets", "build", "compatibility", "core", "diagnostics", "docs",
    "graphics", "input", "licenses", "manifests", "networking", "platform",
    "recompilation", "runtimes", "security", "shortcuts", "stores", "tests",
    "third_party", "tools",
)


def main():
    files = set(ROOT.glob("*.md"))
    files.add(ROOT / "Makefile")
    for name in DIRECTORIES:
        files.update(
            file for file in (ROOT / name).rglob("*")
            if file.is_file() and "__pycache__" not in file.parts and not file.suffix == ".pyc"
        )

    output = ROOT / "dist" / "akeno-gaming-hub-host.zip"
    output.parent.mkdir(exist_ok=True)
    with ZipFile(output, mode="w", compression=ZIP_DEFLATED) as archive:
        for file in sorted(files):
            info = ZipInfo("akeno-gaming-hub/" + file.relative_to(ROOT).as_posix())
            info.external_attr = 0o100644 << 16
            archive.writestr(info, file.read_bytes(), compress_type=ZIP_DEFLATED)
    print(output.relative_to(ROOT))


if __name__ == "__main__":
    main()
