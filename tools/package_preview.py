#!/usr/bin/env python3
"""Assemble manual-copy ZIPs from explicit build outputs and project resources."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.10.1-preview.1"
GAME_SHA256 = "ed5a8f8cd10f9a80d3bda5badd7ba1d4fcfc15996c8c7e018852cb170e264b63"
SDK = ROOT / "third_party/gameinput/4.0.306"
OPENVR = ROOT / "third_party/openvr/0924064316de3effbcd1acf1e309182a2deb1c05"
OPENXR = ROOT / "third_party/openxr/24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def assemble(platform, windows, linux, destination):
    files = {}

    def add(name, source):
        if name in files:
            raise ValueError(f"Duplicate package path: {name}")
        files[name] = source.read_bytes()

    add("Copy-to-game/dinput8.dll", windows / "experimental-proxy/dinput8.dll")
    add("Existing-chain/dinput8.dll", windows / "experimental-chain-proxy/dinput8.dll")
    for name, expected in {
        "GameInputRedist.dll": "156030ed7928791df36ab9033061a151be61251054c45cf3ae65cd460509b04e",
        "GameInputRawInputProxy.exe": "d5d34853ccc9d9f8f19644cf71ffba625d0575bbb80ab19fbc2882b632aee174",
    }.items():
        add("Copy-to-game/" + name, SDK / "native/bin/x64" / name)
        if digest(files["Copy-to-game/" + name]) != expected:
            raise ValueError(f"Pinned SDK file changed: {name}")
    files["Copy-to-game/ed-onfoot-haptics.ini"] = b"[OnFoot]\r\nEnabled=1\r\nStrength=60\r\n"
    helper = "Copy-to-game/ed-vr-haptics/"
    add(helper + "ed-vr-openxr.dll", windows / "openxr/ed-vr-openxr.dll")
    add(helper + "ed-vr-openxr.json", ROOT / "vr/ed-vr-openxr.json")
    if platform == "windows-x64":
        add(helper + "ed-vr-haptics.exe", windows / "ed-vr-haptics.exe")
        add(helper + "openvr_api.dll", OPENVR / "bin/win64/openvr_api.dll")
        document = "INSTALL-WINDOWS.txt"
    else:
        add(helper + "ed-vr-haptics", linux / "ed-vr-haptics")
        add(helper + "libed-vr-openxr.so", linux / "libed-vr-openxr.so")
        add(helper + "libopenvr_api.so", OPENVR / "bin/linux64/libopenvr_api.so")
        add(helper + "launch-with-vr.sh", ROOT / "vr/launch-with-vr.sh")
        document = "INSTALL-LINUX.txt"
    for source in sorted((ROOT / "vr/resources").iterdir()):
        if source.is_file():
            add(helper + "resources/" + source.name, source)
    for name, source in {"GameInput.txt": SDK / "LICENSE.txt", "MinHook.txt": ROOT / "third_party/minhook/LICENSE.txt",
                         "OpenVR.txt": OPENVR / "LICENSE", "OpenXR.txt": OPENXR / "LICENSES/MIT.txt"}.items():
        add("notices/" + name, source)
    registration = (ROOT / "docs/OPENXR.txt").read_text()
    intro, remainder = registration.split("\nWINDOWS\n", 1)
    win, remainder = remainder.split("\nLINUX - NATIVE OPENXR LOADER\n", 1)
    native, limits = remainder.split("\nLIMITS\n", 1)
    selected = "WINDOWS\n" + win if platform == "windows-x64" else "LINUX - NATIVE OPENXR LOADER\n" + native
    files["INSTALL.txt"] = ((ROOT / "docs" / document).read_text() + "\n" + intro + "\n" + selected + "\nLIMITS\n" + limits).encode()
    for name, data in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"Invalid package path: {name}")
        literal_paths = (str(ROOT.resolve()), str(Path.home().resolve()))
        local_paths = literal_paths + tuple(re.sub(r"[^A-Za-z0-9]", "_", path) for path in literal_paths)
        if any(marker.encode(encoding) in data for marker in local_paths for encoding in ("utf-8", "utf-16le")):
            raise ValueError(f"Local build path embedded in {name}")
        if name.endswith("dinput8.dll"):
            offset = struct.unpack_from("<I", data, 0x3c)[0]
            if data[offset:offset + 4] != b"PE\0\0" or struct.unpack_from("<H", data, offset + 4)[0] != 0x8664:
                raise ValueError(f"Expected x64 Windows proxy: {name}")
    manifest = {"schema": 1, "profile": "edup-nhoe", "version": VERSION, "platform": platform,
                "game_sha256": GAME_SHA256, "physical_vr_output_verified": False,
                "gait_boundary_live_verified": False, "automatic_system_configuration": False,
                "preserve_existing": ["ed-onfoot-haptics.ini"],
                "supplied_files_sha256": {name: digest(data) for name, data in sorted(files.items())}}
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / f"EDUP-NHOE-{VERSION}-{platform}.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 17, 0, 0, 0))
            info.create_system = 3
            executable = name.endswith(("/ed-vr-haptics", "/launch-with-vr.sh"))
            info.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files):
            raise ValueError("Archive integrity check failed")
        if any(archive.read(name) != data for name, data in files.items()):
            raise ValueError("Archive member changed")
    checksum = digest(output.read_bytes())
    output.with_suffix(".zip.sha256").write_text(checksum + "  " + output.name + "\n")
    return {"path": str(output), "sha256": checksum, "files": len(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-build", type=Path, required=True)
    parser.add_argument("--linux-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    packages = [assemble(platform, args.windows_build.resolve(), args.linux_build.resolve(), args.output.resolve())
                for platform in ("windows-x64", "linux-proton-x64")]
    print(json.dumps(packages, indent=2))


if __name__ == "__main__":
    main()
