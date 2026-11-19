#!/usr/bin/env python3
"""Hermetic package contract checks using owned build fixtures."""
import importlib.util
import hashlib
import json
import plistlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("package_preview", ROOT / "tools/package_preview.py")
package = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = package
spec.loader.exec_module(package)
from verify_release import verify  # noqa: E402


class PackageContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nhoe-package-check-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.windows, self.linux, self.gamepad, self.native_mac, self.output = (root / name for name in ("windows", "linux", "gamepad", "mac", "output"))
        pe = bytearray(256)
        struct.pack_into("<I", pe, 0x3c, 128)
        pe[128:132] = b"PE\0\0"
        struct.pack_into("<H", pe, 132, 0x8664)
        for build, name, data in (
            (self.windows, "experimental-proxy/dinput8.dll", pe + b"standard"),
            (self.windows, "experimental-chain-proxy/dinput8.dll", pe + b"chain"),
            (self.gamepad, "crossover-dinput8/dinput8.dll", pe + b"crossover"),
            (self.windows, "openxr/ed-vr-openxr.dll", b"owned Windows layer"),
            (self.windows, "ed-vr-haptics.exe", b"owned Windows helper"),
            (self.linux, "ed-vr-haptics", b"owned Linux helper"),
            (self.linux, "libed-vr-openxr.so", b"owned Linux layer"),
        ):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        app=self.native_mac / "ed-macos-haptics.app/Contents"
        (app / "MacOS").mkdir(parents=True)
        (app / "_CodeSignature").mkdir()
        (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier":"org.fenris159.edup-nhoe.haptics",
            "CFBundleExecutable":"ed-macos-haptics","CFBundleVersion":"0.12.1"}))
        (app / "_CodeSignature/CodeResources").write_bytes(plistlib.dumps({"files2":{}}))
        macho=bytearray(112)
        struct.pack_into(">II",macho,0,0xcafebabe,2)
        for i,cpu in enumerate((0x01000007,0x0100000c)):
            offset=48+i*32
            struct.pack_into(">IIIII",macho,8+i*20,cpu,0,offset,32,0)
            struct.pack_into("<IIII",macho,offset,0xfeedfacf,cpu,0,2)
        (app / "MacOS/ed-macos-haptics").write_bytes(macho)

    def assemble(self):
        return package.assemble_all(self.windows, self.linux, self.gamepad, self.native_mac, self.output)

    def test_all_platforms_with_minimal_mac_and_distinct_vr_payloads(self):
        results = self.assemble()
        self.assertEqual(len(results), 3)
        archives = {}
        for result in results:
            with zipfile.ZipFile(result["path"]) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                archives[manifest["platform"]] = {name: archive.read(name) for name in archive.namelist()}
        self.assertEqual(set(archives), {"windows-x64", "linux-proton-x64", "macos-crossover-x64"})
        for files in archives.values():
            self.assertIn(b"[ShipCombat]\r\nEnabled=1\r\nStrength=60\r\n",
                          files["Copy-to-game/ed-onfoot-haptics.ini"])
        mac = archives["macos-crossover-x64"]
        self.assertEqual(set(mac), {"Copy-to-game/dinput8.dll", "Copy-to-game/GameInputRedist.dll",
                         "Copy-to-game/GameInputRawInputProxy.exe", "Copy-to-game/ed-onfoot-haptics.ini",
                         "Copy-to-game/ed-vr-haptics.ini", "INSTALL.txt", "notices/GameInput.txt",
                         "notices/MinHook.txt", "manifest.json", "Copy-to-game/ed-macos-haptics.app/Contents/Info.plist",
                         "Copy-to-game/ed-macos-haptics.app/Contents/MacOS/ed-macos-haptics",
                         "Copy-to-game/ed-macos-haptics.app/Contents/_CodeSignature/CodeResources"})
        self.assertEqual(mac["Copy-to-game/dinput8.dll"], (self.gamepad / "crossover-dinput8/dinput8.dll").read_bytes())
        self.assertEqual(mac["Copy-to-game/ed-vr-haptics.ini"], b"[VR]\r\nEnabled=0\r\n")
        self.assertIn(b"Output=auto", mac["Copy-to-game/ed-onfoot-haptics.ini"])
        self.assertIn(b"Native then Builtin", mac["INSTALL.txt"])
        self.assertFalse(json.loads(mac["manifest.json"])["mac_physical_output_verified"])
        windows, linux = archives["windows-x64"], archives["linux-proton-x64"]
        self.assertIn("Copy-to-game/ed-vr-haptics/ed-vr-haptics.exe", windows)
        self.assertNotIn("Copy-to-game/ed-vr-haptics/libed-vr-openxr.so", windows)
        self.assertIn("Copy-to-game/ed-vr-haptics/libed-vr-openxr.so", linux)
        self.assertNotIn("Copy-to-game/ed-vr-haptics/ed-vr-haptics.exe", linux)
        self.assertEqual(len(verify(self.output)), 6)

    def test_missing_mac_artifact_prevents_partial_release(self):
        (self.gamepad / "crossover-dinput8/dinput8.dll").unlink()
        with self.assertRaises(FileNotFoundError):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_wrong_proxy_architecture_prevents_partial_release(self):
        path = self.gamepad / "crossover-dinput8/dinput8.dll"
        data = bytearray(path.read_bytes())
        struct.pack_into("<H", data, 132, 0x14c)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Expected x64"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_non_universal_native_app_prevents_partial_release(self):
        path=self.native_mac / "ed-macos-haptics.app/Contents/MacOS/ed-macos-haptics"
        path.write_bytes(b"not a universal executable")
        with self.assertRaisesRegex(ValueError,"Expected universal"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_previous_mac_checker_prevents_stale_multi_platform_release(self):
        path = self.native_mac / "ed-macos-haptics.app/Contents/Info.plist"
        info = plistlib.loads(path.read_bytes())
        info["CFBundleVersion"] = "0.12.0"
        path.write_bytes(plistlib.dumps(info))
        with self.assertRaisesRegex(ValueError, "Expected native Mac companion 0.12.1"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_release_check_rejects_changed_asset(self):
        first = Path(self.assemble()[0]["path"])
        first.write_bytes(first.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            verify(self.output)

    def test_release_check_requires_mac_checksum(self):
        results = self.assemble()
        mac = next(Path(result["path"]) for result in results if "macos" in result["path"])
        mac.with_suffix(".zip.sha256").unlink()
        with self.assertRaises(FileNotFoundError):
            verify(self.output)

    def rewrite_with_valid_hashes(self, path, remove=None, add=None):
        with zipfile.ZipFile(path) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        if remove:
            del files[remove]
        if add:
            files[add] = b"unexpected platform helper"
        manifest = json.loads(files.pop("manifest.json"))
        manifest["supplied_files_sha256"] = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
        files["manifest.json"] = json.dumps(manifest).encode()
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in files.items():
                archive.writestr(name, data)
        path.with_suffix(".zip.sha256").write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")

    def test_release_check_rejects_missing_required_files_despite_valid_hashes(self):
        results = self.assemble()
        for platform, member in (("macos-crossover-x64", "Copy-to-game/dinput8.dll"),
                                 ("windows-x64", "INSTALL.txt"),
                                 ("linux-proton-x64", "Copy-to-game/ed-vr-haptics/libed-vr-openxr.so")):
            with self.subTest(platform=platform):
                path = next(Path(result["path"]) for result in results if result["path"].endswith(platform + ".zip"))
                original = path.read_bytes()
                checksum = path.with_suffix(".zip.sha256").read_text()
                try:
                    self.rewrite_with_valid_hashes(path, remove=member)
                    with self.assertRaisesRegex(ValueError, "Platform contents mismatch"):
                        verify(self.output)
                finally:
                    path.write_bytes(original)
                    path.with_suffix(".zip.sha256").write_text(checksum)

    def test_release_check_rejects_mac_linux_payload_despite_valid_hashes(self):
        results = self.assemble()
        path = next(Path(result["path"]) for result in results if "macos" in result["path"])
        self.rewrite_with_valid_hashes(path, add="Copy-to-game/ed-vr-haptics/libed-vr-openxr.so")
        with self.assertRaisesRegex(ValueError, "Platform contents mismatch"):
            verify(self.output)


if __name__ == "__main__":
    unittest.main()
