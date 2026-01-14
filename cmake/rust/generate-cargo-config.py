#!/usr/bin/env python3
#
# Generates a Cargo configuration file which overrides certain dependencies with local paths.
# Used to ensure that each module uses the everestrs version managed by CMake when built by CMake.
#
# This script is needed because Cargo requires you to specify the source (crates.io, Git URL, ...)
# of a dependency in order to override it. Hardcoding it would break forks/alternative sources,
# so this script generates a patch configuration file dynamically. It looks something like this:
#
# [patch."https://github.com/everest/everest-core.git"]
# everestrs = { path = "/foo" }
# everestrs-build = { path = "/bar" }

# pyright: reportAny=false, reportUnusedCallResult=false
import argparse
import json
import subprocess
from os.path import realpath

# Cargo profile definitions that correspond to CMake configurations like MinSizeRel.
CARGO_PROFILES = """
[profile.minsizerel]
inherits = "release"
opt-level = "s"
debug = false

[profile.relwithdebinfo]
inherits = "release"
debug = true
"""


def parse_package_id(package_id: str) -> tuple[str, str, str]:
    """Parse a qualified Cargo package ID into its name, kind and source components: https://doc.rust-lang.org/cargo/reference/pkgid-spec.html"""
    kind, tail = package_id.split("+", 1)
    if "#" in tail:
        source, tail = tail.rsplit("#", 1)
    else:
        source = tail

    for sep in ["@", ":"]:
        if sep in tail:
            name = tail.rsplit(sep, 1)[0]
            return name, kind, source

    if "/" in source:
        name = source.rsplit("/", 1)[-1]
        return name, kind, source

    return tail, kind, "crates.io"


def path_from_source(source: str) -> str:
    """Convert a Cargo source string to a filesystem path."""
    if source.startswith("file://"):
        return realpath(source.removeprefix("file://"))
    raise ValueError(f"Source '{source}' is not a file URL")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=str, required=True)
    parser.add_argument("--cargo", type=str, default="cargo")
    parser.add_argument("--manifest-path", type=str, required=True)
    parser.add_argument("--target", type=str, required=True)
    parser.add_argument("overrides", type=str, nargs="+")
    args = parser.parse_args()

    # Parse the key=value overrides into a dictionary.
    overrides: dict[str, str] = {}
    for override in args.overrides:
        name, path = override.split("=", 1)
        overrides[name] = realpath(path)

    # Request the crate/workspace's metadata from Cargo.
    cargo_cmd = [
        args.cargo,
        "metadata",
        "--quiet",
        "--format-version=1",
        f"--manifest-path={args.manifest_path}",
        f"--filter-platform={args.target}",
    ]
    metadata_json = json.loads(subprocess.check_output(cargo_cmd, text=True))

    # Find all packages we want to override in the resolved dependency graph.
    patches: dict[str, list[tuple[str, str]]] = {}
    for package in metadata_json["resolve"]["nodes"]:
        name, kind, src = parse_package_id(package["id"])
        if name not in overrides:
            continue

        src = src.split("?", 1)[0]  # Ignored for patches
        new_src = overrides.pop(name)
        if not (kind == "path" and realpath(src.removeprefix("file://")) == new_src):
            patches.setdefault(src, []).append((name, new_src))
        if not overrides:
            break

    # Write out the Cargo configuration file.
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(CARGO_PROFILES)
        for source, entries in patches.items():
            f.write(f'[patch."{source}"]\n')
            for name, path in entries:
                f.write(f'{name} = {{ path = "{path}" }}\n')


if __name__ == "__main__":
    main()
