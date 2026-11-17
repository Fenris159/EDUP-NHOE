#!/usr/bin/env python3
"""Native Linux OpenXR layer/relay checks using owned boundaries only."""
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SDK = ROOT / "third_party/openxr/24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(arguments):
    result = subprocess.run(arguments, text=True, capture_output=True, timeout=100)
    return dict(command=[str(a) for a in arguments], returncode=result.returncode,
                stdout=result.stdout.strip(), stderr=result.stderr.strip())


def main():
    folder = BUILD / "openxr-native-checks" / str(time.time_ns())
    folder.mkdir(parents=True)
    files = ["vr/openxr_layer.cpp", "vr/openxr_layer.map", "vr/native_xr_relay.cpp", "vr/native_xr_relay.hpp", "vr/udp_client.hpp",
             "include/edtrace/vr_platform.hpp", "include/edtrace/vr_backend.hpp", "include/edtrace/vr_haptics.hpp", "include/edtrace/haptic_frame.hpp",
             "include/edtrace/openxr_haptics_status.hpp", "include/edtrace/openxr_haptic_routing.hpp", "include/edtrace/openxr_native_cache.hpp", "include/edtrace/openxr_native_identity.hpp",
             "verification/openxr_haptics.cpp", "verification/openxr_native.cpp", "verification/openxr_native_relay.cpp", "verification/openxr_native_verify.py"]
    sources = {name: digest(ROOT / name) for name in files}
    provenance = json.loads((SDK / "provenance.json").read_text())
    assert all(digest(SDK / name) == item["sha256"] for name, item in provenance["files"].items())
    report = dict(scope="Owned native Linux mock OpenXR loader/runtime, controlled clock/network boundary, literal mapping identity, no game launch, no actual XR runtime/controllers/input or physical haptics.",
                  source_sha256=sources, sdk=provenance, runs=[], mutations=[], physical_output_verified=False)
    common = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(ROOT / "include"),
              "-isystem", str(SDK / "include")]
    library = folder / "libed-vr-openxr.so"
    result = run(common + ["-shared", "-fPIC", "-fvisibility=hidden", str(ROOT / "vr/openxr_layer.cpp"), str(ROOT / "vr/native_xr_relay.cpp"),
                           "-pthread", "-ldl", "-Wl,--version-script=" + str(ROOT / "vr/openxr_layer.map"), "-o", str(library)])
    assert result["returncode"] == 0, result
    report["production_build"] = result
    exports = run(["nm", "-D", "--defined-only", str(library)])
    assert exports["returncode"] == 0 and {line.split()[-1] for line in exports["stdout"].splitlines()} == {
        "EDOpenXRGetStatus", "xrNegotiateLoaderApiLayerInterface"}, exports
    report["production_exports"] = exports["stdout"]
    cases = [
        ("layer", ["-DED_OPENXR_FIXTURE=1", str(ROOT / "verification/openxr_haptics.cpp"), str(ROOT / "vr/openxr_layer.cpp"), str(ROOT / "vr/native_xr_relay.cpp"), "-pthread", "-ldl"]),
        ("cache-identity", [str(ROOT / "verification/openxr_native.cpp")]),
        ("relay-worker", ["-DED_NATIVE_XR_RELAY_FIXTURE=1", str(ROOT / "verification/openxr_native_relay.cpp"), str(ROOT / "vr/native_xr_relay.cpp"), "-pthread"]),
    ]
    binaries = {str(library): digest(library)}
    for name, args in cases:
        exe = folder / name
        compiled = run(common + args + ["-o", str(exe)])
        assert compiled["returncode"] == 0, compiled
        result = run([str(exe)])
        assert result["returncode"] == 0 and result["stdout"].startswith("PASS "), result
        binaries[str(exe)] = digest(exe)
        report["runs"].append(dict(name=name, **result))
    mutations = [
        ("ready-expiry", "include/edtrace/openxr_native_cache.hpp", "now-reported<kNativeXrReadyLeaseMs", "now-reported<=kNativeXrReadyLeaseMs", "cache", "render stall expires readiness at 100 ms"),
        ("frame-expiry", "include/edtrace/openxr_native_cache.hpp", "now-received>=kNativeXrFrameLeaseMs", "now-received>kNativeXrFrameLeaseMs", "cache", "cached relay frame expires at 100 ms"),
        ("cached-epoch", "include/edtrace/openxr_native_cache.hpp", "epoch!=expected_epoch", "(epoch!=expected_epoch&&false)", "cache", "prior readiness epoch cannot replay cached output"),
        ("standby-frame", "include/edtrace/openxr_native_cache.hpp", "!known||!owned", "!known||(!owned&&false)", "cache", "standby relay response never becomes output"),
        ("other-pe-mapping", "include/edtrace/openxr_native_identity.hpp", "number==expected_inode", "number!=0", "cache", "other inode cannot activate native layer"),
        ("worker-heartbeat", "vr/native_xr_relay.cpp", "const bool armed=NativeXrRecentlyReady(ready.load(std::memory_order_acquire),Clock(),last_ready.load(std::memory_order_acquire));", "const bool armed=ready.load(std::memory_order_acquire);", "worker", "render heartbeat expiration reaches network readiness"),
        ("worker-ready-epoch", "vr/native_xr_relay.cpp", "if(relay.ready.exchange(wanted,std::memory_order_acq_rel)!=wanted) relay.epoch.fetch_add(1,std::memory_order_acq_rel);", "relay.ready.store(wanted,std::memory_order_release);", "worker", "immediate focus recovery cannot replay prior readiness epoch"),
        ("native-route-id", "vr/openxr_layer.cpp", "constexpr edtrace::VrBackend kBackend=edtrace::VrBackend::NativeOpenXR;", "constexpr edtrace::VrBackend kBackend=edtrace::VrBackend::OpenXR;", "layer", "OpenXR uses only its output route"),
    ]
    for name, relative, before, after, kind, expected in mutations:
        original = (ROOT / relative).read_text()
        assert original.count(before) == 1, name
        place = folder / name
        place.mkdir()
        exe = place / "verify"
        if kind == "cache":
            path = place / relative.removeprefix("include/")
            path.parent.mkdir(parents=True)
            path.write_text(original.replace(before, after))
            args = ["-I" + str(place), str(ROOT / "verification/openxr_native.cpp")]
        else:
            path = place / "source.cpp"
            path.write_text(original.replace(before, after))
            args = ["-I" + str(ROOT / "vr")]
            if kind == "worker":
                args += ["-DED_NATIVE_XR_RELAY_FIXTURE=1", str(ROOT / "verification/openxr_native_relay.cpp"), str(path), "-pthread"]
            else:
                args += ["-DED_OPENXR_FIXTURE=1", str(ROOT / "verification/openxr_haptics.cpp"), str(path), str(ROOT / "vr/native_xr_relay.cpp"), "-pthread", "-ldl"]
        # Header overrides must precede production include paths.
        compiled = run(common[:7] + args + common[7:] + ["-o", str(exe)])
        assert compiled["returncode"] == 0, compiled
        result = run([str(exe)])
        assert result["returncode"] != 0 and expected in result["stderr"], (name, result)
        report["mutations"].append(dict(name=name, detected=True, **result))
        print("mutation", name, "detected", flush=True)
    assert sources == {name: digest(ROOT / name) for name in sources}
    report.update(passed=True, source_unchanged=True, binary_sha256=binaries)
    output = BUILD / "openxr-native-verification.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS", output, flush=True)


if __name__ == "__main__":
    main()
