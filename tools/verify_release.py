#!/usr/bin/env python3
"""Validate the complete three-platform release; optionally publish it."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from package_preview import ROOT, VERSION

REPOSITORY = "Fenris159/EDUP-NHOE"
RELEASE_PLATFORMS = ("windows-x64", "linux-proton-x64", "macos-crossover-x64")


def expected_members(platform):
    # Keep this contract independent of the assembler and archive manifest.
    common = {"Copy-to-game/dinput8.dll", "Copy-to-game/GameInputRedist.dll",
              "Copy-to-game/GameInputRawInputProxy.exe", "Copy-to-game/ed-onfoot-haptics.ini",
              "INSTALL.txt", "manifest.json", "notices/GameInput.txt", "notices/MinHook.txt"}
    if platform == "macos-crossover-x64":
        return common | {"Copy-to-game/ed-vr-haptics.ini", "Copy-to-game/ed-macos-haptics.app/Contents/Info.plist",
                         "Copy-to-game/ed-macos-haptics.app/Contents/MacOS/ed-macos-haptics",
                         "Copy-to-game/ed-macos-haptics.app/Contents/_CodeSignature/CodeResources"}
    vr = {"Existing-chain/dinput8.dll", "notices/OpenVR.txt", "notices/OpenXR.txt"}
    vr |= {"Copy-to-game/ed-vr-haptics/" + name for name in (
        "ed-vr-openxr.dll", "ed-vr-openxr.json", "resources/actions.json",
        "resources/bindings_generic.json", "resources/bindings_knuckles.json",
        "resources/bindings_oculus_touch.json", "resources/bindings_vive_controller.json",
        "resources/haptics.vrmanifest")}
    platform_files = {
        "windows-x64": ("ed-vr-haptics.exe", "openvr_api.dll"),
        "linux-proton-x64": ("ed-vr-haptics", "libed-vr-openxr.so", "libopenvr_api.so", "launch-with-vr.sh"),
    }
    return common | vr | {"Copy-to-game/ed-vr-haptics/" + name for name in platform_files[platform]}


def verify(directory):
    assets = []
    for platform in RELEASE_PLATFORMS:
        path = directory / f"EDUP-NHOE-{VERSION}-{platform}.zip"
        checksum = path.with_suffix(".zip.sha256")
        expected = hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n"
        if checksum.read_text() != expected:
            raise ValueError(f"Checksum mismatch: {path.name}")
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or archive.testzip() is not None:
                raise ValueError(f"Invalid archive: {path.name}")
            if set(names) != expected_members(platform):
                raise ValueError(f"Platform contents mismatch: {path.name}")
            manifest = json.loads(archive.read("manifest.json"))
            if manifest["version"] != VERSION or manifest["platform"] != platform:
                raise ValueError(f"Release identity mismatch: {path.name}")
            members = manifest["supplied_files_sha256"]
            if set(names) != set(members) | {"manifest.json"}:
                raise ValueError(f"Unlisted package files: {path.name}")
            if any(hashlib.sha256(archive.read(name)).hexdigest() != digest for name, digest in members.items()):
                raise ValueError(f"Package member mismatch: {path.name}")
        assets.extend((path, checksum))
    return assets


def command(*arguments):
    return subprocess.check_output(arguments, cwd=ROOT, text=True).strip()


def publish(assets, notes):
    if command("git", "status", "--porcelain"):
        raise ValueError("Commit the reviewed source before publishing")
    commit = command("git", "rev-parse", "HEAD")
    tag = "v" + VERSION
    if command("git", "rev-parse", tag + "^{commit}") != commit:
        raise ValueError("Release tag must identify the reviewed source commit")
    remote = command("gh", "api", f"repos/{REPOSITORY}/git/ref/heads/main", "--jq", ".object.sha")
    remote_tag = command("gh", "api", f"repos/{REPOSITORY}/git/ref/tags/{tag}", "--jq", ".object.sha")
    if remote != commit or remote_tag != commit:
        raise ValueError("Push the reviewed commit to public main and its lightweight release tag first")
    pages = json.loads(command("gh", "api", f"repos/{REPOSITORY}/commits/{commit}/check-runs", "--paginate", "--slurp"))
    checks = [check for page in pages for check in page["check_runs"]]
    for name in ("native", "windows", "macos"):
        matches = [check for check in checks if check["name"] == name]
        if not matches or any(check["conclusion"] != "success" for check in matches):
            raise ValueError(f"Required CI check has not succeeded: {name}")
    subprocess.run(["gh", "release", "create", tag, *map(str, assets), "--repo", REPOSITORY,
                    "--verify-tag", "--prerelease", "--title", f"EDUP - NHOE {VERSION}",
                    "--notes-file", str(notes)], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "dist")
    parser.add_argument("--publish", action="store_true")
    parser.add_argument("--notes-file", type=Path)
    args = parser.parse_args()
    if args.publish and (not args.notes_file or not args.notes_file.is_file()):
        parser.error("--publish requires an existing --notes-file")
    assets = verify(args.directory.resolve())
    print("PASS all three packages, member hashes and six release assets", flush=True)
    if args.publish:
        publish(assets, args.notes_file.resolve())


if __name__ == "__main__":
    main()
