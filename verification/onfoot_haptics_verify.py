#!/usr/bin/env python3
"""Run the pure policy and detect deliberately broken copied implementations."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ["src/onfoot_haptics.cpp", "include/edtrace/onfoot_haptics.hpp", "include/edtrace/onfoot_profiles.hpp",
         "include/edtrace/haptic_frame.hpp", "verification/onfoot_haptics.cpp", "verification/onfoot_haptics_verify.py"]


def main():
    original = (ROOT / FILES[0]).read_text()
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in FILES}
    results = []
    with tempfile.TemporaryDirectory(prefix="ed-onfoot-policy-") as temporary:
        folder = Path(temporary)

        def run(name, source):
            copied = folder / (name + ".cpp")
            copied.write_text(source)
            exe = folder / name
            built = subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                "-I" + str(ROOT / "include"), str(ROOT / "verification/onfoot_haptics.cpp"), str(copied), "-o", str(exe)], capture_output=True, text=True, timeout=60)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            return {"name": name, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}

        baseline = run("baseline", original)
        assert baseline["returncode"] == 0, baseline
        mutants = {
            "walking_enabled": ("if (!actor->speed_known || actor->speed <= kMaxWalkingSpeed) return;", "if (!actor->speed_known) return;"),
            "running_disabled": ("actor->speed <= kMaxWalkingSpeed", "actor->speed >= kMaxWalkingSpeed"),
            "gait_survives_focus_loss": ("for (auto& actor : actors_) actor.speed_known = false;", "/* copied fault: retain old gait */"),
            "inactive_gait_enabled": ("actor->speed_known = active_ &&", "actor->speed_known ="),
            "vehicle_requires_inside": ("!actor->attached || !actor->vehicle_view", "!actor->attached || !actor->inside || !actor->vehicle_view"),
            "vehicle_ignores_attachment": ("!actor->attached || !actor->vehicle_view", "!actor->vehicle_view"),
            "missile_on_projectile": ("input.id == 313889439", "input.id == 1591166666"),
            "jetpack_wrong_start": ("input.id == 3666194424", "input.id == 3666194425"),
            "jetpack_ignores_cutoff": ("StopJetpack(input.object,now);", "/* copied fault: missing jetpack cutoff */"),
            "jetpack_extends_watchdog": ("!value.stopping && now < value.deadline", "false"),
            "ignore_unattached": ("!active_ || !actor->attached || !actor->inside", "!active_ || !actor->inside"),
            "invent_first_person": ("if (!actor->first) { ++stats_.rejected; return; }", "if (false) { ++stats_.rejected; return; }"),
            "accept_failed_post": ("|| !input.playing ||", "|| false ||"),
            "ignore_release": ("if (!route.start) { StopHold(input.object);", "if (!route.start) {"),
            "ignore_watchdog": ("hold.object && now >= hold.deadline", "false"),
            "ignore_unregistration": ("if (input.result == 1 && actor) { ClearOwner(input.object); *actor = {}; }", "if (input.result == 1 && actor) { *actor = {}; }"),
            "duplicate_landing": ("if (Cue(now, group))", "if ((Cue(now, group), true))"),
        }
        for name, (old, new) in mutants.items():
            assert old in original, name
            result = run(name, original.replace(old, new))
            assert result["returncode"] != 0 and "FAIL " in result["stderr"], result
            results.append(result)
            print(name, "detected", flush=True)
    assert hashes == {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in FILES}
    report = {"passed": True, "scope": "Pure policy only: no game, operating-system controller API or hardware output.",
              "source_sha256": hashes, "baseline": baseline, "mutations": results}
    (ROOT / "build/onfoot-haptics-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(baseline["stdout"].strip())


if __name__ == "__main__":
    main()
