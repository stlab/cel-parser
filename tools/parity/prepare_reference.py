"""Builds a test-only Rust driver from an explicitly supplied local source checkout."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def prepare(source, build):
    """Builds core packages from canonical paths and locks completed manifest transitions.

    Seeds changed or unfinished manifests from the canonical lockfile. Repeated successful
    preparations require the generated resolved lock unchanged. Never writes the source checkout.
    """
    source, build = Path(source).resolve(), Path(build).resolve()
    for package in ("cel-parser", "cel-runtime"):
        if not (source / package / "Cargo.toml").is_file():
            raise FileNotFoundError(f"required Rust reference package: {source / package}")
    if not (source / "Cargo.lock").is_file():
        raise FileNotFoundError("enabled parity gate requires the reference Cargo.lock")
    cargo = shutil.which("cargo")
    if cargo is None:
        raise FileNotFoundError("enabled parity gate requires Cargo")
    build.mkdir(parents=True, exist_ok=True)
    driver = Path(__file__).with_name("reference.rs").resolve()
    dependencies = "\n".join(
        f'{p} = {{ path = {json.dumps(str(source / p))} }}'
        for p in ("cel-parser", "cel-runtime"))
    manifest = (
        '[package]\nname = "cel-parity-reference"\nversion = "0.0.0"\n'
        'edition = "2024"\n[workspace]\n[dependencies]\nanyhow = "1.0"\n'
        + dependencies + '\n[[bin]]\nname = "cel-parity-reference"\npath = '
        + json.dumps(str(driver)) + '\n')
    (build / "Cargo.toml").write_text(manifest, encoding="utf-8")
    lock = build / "Cargo.lock"
    completed = build / ".prepared-manifest"
    initialized = (lock.is_file() and completed.is_file()
                   and completed.read_text(encoding="utf-8") == manifest)
    if not initialized:
        shutil.copyfile(source / "Cargo.lock", lock)
    command = [cargo, "build", "--manifest-path", str(build / "Cargo.toml"), "--release"]
    if initialized:
        command.append("--locked")
    subprocess.run(command, check=True)
    completed.write_text(manifest, encoding="utf-8")


def main():
    """Accepts local reference and generated-manifest paths, never an installed dependency."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--build", required=True)
    args = parser.parse_args()
    prepare(args.source, args.build)


if __name__ == "__main__":
    main()
