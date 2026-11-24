"""Package qtoolkit_verify as a release asset.

The verifier links no CUDA target and needs no toolkit: it is the half of the
product a GPU-less reader can run, so it can ship as one archive instead of a
build. The archive carries the binary, the pins file check 5 compares against,
the committed artifacts to try it on, and a README that says what to run.

Usage:
    python tools/package_verify.py --binary build/qtoolkit_verify --out dist

The platform tag comes from the binary's extension: `.exe` is windows-x64, a bare
name is linux-x64. The version comes from VERSION, the only owner of it.
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

USAGE = """qtoolkit_verify — verify a report.json without a GPU and without CMake.

  ./qtoolkit_verify artifacts/report.json

Exit 0 means every comparison held. 1 names the comparison that failed, with
both values. 2 means it cannot decide: a missing artifact, or pins that disagree
with pins.cmake.

The artifacts beside this file are the ones the report was produced from, so the
command above exits 0 on an untouched checkout. Point it at another directory to
verify a report you moved:

  ./qtoolkit_verify path/to/report.json path/to/artifacts

Check 5 compares report.pins with pins.cmake. This archive ships that file beside
the binary, and the verifier looks there first; set QTOOLKIT_PINS_FILE to point
it somewhere else.

Built for {platform}. The Linux binary links libstdc++ statically and needs the
glibc of the runner it was built on (Ubuntu 22.04, 2.35). The Windows binary
needs the Microsoft Visual C++ 2015-2022 redistributable.
"""


def platform_tag(binary: Path) -> str:
    return "windows-x64" if binary.suffix.lower() == ".exe" else "linux-x64"


def version() -> str:
    text = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not text:
        raise SystemExit("VERSION is empty")
    return text


def collect(binary: Path) -> list[tuple[Path, str]]:
    """The archive's members, as (source, name inside the archive)."""
    members: list[tuple[Path, str]] = [
        (binary, binary.name),
        (ROOT / "cmake" / "pins.cmake", "pins.cmake"),
        (ROOT / "LICENSE", "LICENSE"),
    ]
    artifacts = ROOT / "artifacts"
    for path in sorted(artifacts.iterdir()):
        if path.is_file():
            members.append((path, f"artifacts/{path.name}"))
    return members


def write_archive(members: list[tuple[Path, str]], readme: str, destination: Path,
                  binary: Path) -> None:
    with tempfile.TemporaryDirectory() as scratch:
        staging = Path(scratch) / "qtoolkit_verify"
        staging.mkdir()
        for source, name in members:
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        (staging / "README.txt").write_text(readme, encoding="utf-8", newline="\n")

        if binary.suffix.lower() == ".exe":
            with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(staging.rglob("*")):
                    if path.is_file():
                        # One root directory, as the tar has: unzipping in a
                        # shared Downloads folder must not scatter the artifacts.
                        archive.write(path, Path("qtoolkit_verify") / path.relative_to(staging))
        else:
            with tarfile.open(destination, "w:gz") as archive:
                archive.add(staging, arcname="qtoolkit_verify")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Package qtoolkit_verify for a release asset.")
    parser.add_argument("--binary", required=True, type=Path, help="the built verifier")
    parser.add_argument("--out", default=ROOT / "dist", type=Path, help="where the archive lands")
    arguments = parser.parse_args(argv)

    binary = arguments.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"package_verify: {binary} is not a file")

    tag = platform_tag(binary)
    name = f"qtoolkit_verify-{version()}-{tag}"
    suffix = ".zip" if binary.suffix.lower() == ".exe" else ".tar.gz"
    arguments.out.mkdir(parents=True, exist_ok=True)
    destination = arguments.out / f"{name}{suffix}"

    members = collect(binary)
    write_archive(members, USAGE.format(platform=tag), destination, binary)

    size = destination.stat().st_size
    print(f"{destination} ({size} bytes, {len(members)} files + README.txt)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
