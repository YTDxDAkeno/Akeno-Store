"""Create a deterministic source distribution for a Python 3.12+ host."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = (
    "apps", "assets", "build", "compatibility", "core", "docs", "licenses", "manifests",
    "native", "ps5", "runtimes", "security", "stores", "tests", "tools",
)
# Generated or downloaded trees stay out of the source package.
EXCLUDED = {"__pycache__", ".deps", "dist"}


def main():
    files = set(ROOT.glob("*.md"))
    files.add(ROOT / "Makefile")
    files.add(ROOT / "LICENSE")
    for name in DIRECTORIES:
        files.update(
            file for file in (ROOT / name).rglob("*")
            if file.is_file() and not EXCLUDED.intersection(file.parts) and not file.suffix == ".pyc"
            and not (name in {"native", "ps5"} and "build" in file.relative_to(ROOT / name).parts)
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
