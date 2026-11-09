"""Run the Princeton Road Jolt route in its own Godot project, user cache and preferences."""

import argparse
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2]


def executable(command):
    if Path(command).is_file():
        return str(Path(command).resolve())
    located = shutil.which(command)
    if not located:
        raise ValueError(f"Executable unavailable: {command}")
    return located


def setup(work, godot, bun):
    work.mkdir(parents=True, exist_ok=True)
    project = work / "game"
    helper = work / "helper"
    name = (
        "Sunday Driver Route QA " + hashlib.sha256(str(work).encode()).hexdigest()[:12]
    )
    if not (work / "ready").exists():
        shutil.copytree(
            SOURCE / "game",
            project,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(".godot"),
        )
        settings = project / "project.godot"
        settings.write_text(
            settings.read_text().replace(
                'config/name="Sunday Driver"', f'config/name="{name}"'
            )
        )
        shutil.copytree(SOURCE / "helper", helper, dirs_exist_ok=True)
        for source in [SOURCE / "package.json", SOURCE / "bun.lock"]:
            shutil.copy2(source, helper / source.name)
        subprocess.run([bun, "install", "--frozen-lockfile"], cwd=helper, check=True)
        tool = work / "tools" / "atlas"
        tool.parent.mkdir(exist_ok=True)
        paths = (
            bun,
            str(SOURCE / "lab/sustained/audit-fetch.mjs"),
            str(helper / "atlas.mjs"),
        )
        tool.write_text(
            '#!/bin/sh\nexec %s --preload %s %s "$@"\n' % tuple(map(shlex.quote, paths))
        )
        tool.chmod(0o755)
        # The source edition deliberately omits Godot's generated import files.
        with (work / "import.log").open("w") as log:
            subprocess.run(
                [godot, "--headless", "--import", "--path", str(project)],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=180,
            )
        (work / "ready").write_text(name + "\n")
    data_home = (
        (Path.home() / "Library/Application Support")
        if sys.platform == "darwin"
        else Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    )
    user = (
        data_home / "Godot" / "app_userdata" / name
        if sys.platform == "darwin"
        else data_home / "godot" / "app_userdata" / name
    )
    user.mkdir(exist_ok=True, parents=True)
    cache = work / "cache"
    cache.mkdir(exist_ok=True)
    link = user / "map-cache"
    if not link.exists() and not link.is_symlink():
        link.symlink_to(cache, target_is_directory=True)
    if link.resolve() != cache:
        raise ValueError(f"Isolated QA cache already points elsewhere: {link}")
    return project, cache


def run(args):
    work = args.work.expanduser().resolve()
    godot = executable(args.godot)
    if work == SOURCE or SOURCE in work.parents:
        raise ValueError("Choose a scratch directory outside the source tree.")
    project, cache = setup(work, godot, executable(args.bun))
    out = work / args.name
    out.mkdir(exist_ok=False)
    cold = not any(cache.iterdir())
    env = os.environ.copy()
    env.update(
        SUSTAINED_AUDIT=str(out / "source.jsonl"),
        SUSTAINED_OUTPUT=str(out),
        SUSTAINED_SCREEN="1" if args.mode == "metal" else "",
    )
    options = [
        "--path",
        str(project),
        "--audio-driver",
        "Dummy",
        "--log-file",
        str(out / "engine.log"),
    ]
    if args.mode == "metal":
        if sys.platform != "darwin":
            raise ValueError("The Metal sample requires macOS.")
        options += [
            "--windowed",
            "--position",
            "4600,22",
            "--resolution",
            "960x600",
            "--max-fps",
            "60",
        ]
    else:
        options += ["--headless"]
        options += (
            ["--max-fps", "65"] if args.mode == "realtime" else ["--fixed-fps", "60"]
        )
    options += ["res://checks/SustainedRoute.tscn"]
    if args.mode == "metal":
        app = next(p for p in Path(godot).parents if p.suffix == ".app")
        command = ["open", "-g", "-j", "-n", "-W", "-a", str(app)]
        for key in ("SUSTAINED_AUDIT", "SUSTAINED_OUTPUT", "SUSTAINED_SCREEN"):
            command += ["--env", key + "=" + env[key]]
        command += ["--args", *options]
    else:
        command = [godot, *options]
    with (out / "stdout.log").open("w") as log:
        subprocess.run(
            command,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=225,
        )
    result = json.loads((out / "result.json").read_text())
    entries = list(map(json.loads, (out / "source.jsonl").read_text().splitlines()))
    http = [entry for entry in entries if entry["event"] == "http"]
    network = {
        "initially_empty_source_cache": cold,
        "requests": len(http),
        "declared_response_bytes": sum(int(e.get("content_length") or 0) for e in http),
        "hosts": dict(Counter(e["host"] for e in http)),
    }
    (out / "network.json").write_text(json.dumps(network, indent=2) + "\n")
    print(
        json.dumps(
            {
                "route_passed": result["pass"],
                "metres": result["distance_m"],
                "seams": len(result["seams"]),
                "braking": result["brake_test"],
                "network": network,
            },
            indent=2,
        )
    )
    if not result["pass"]:
        sys.exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--work",
        type=Path,
        required=True,
        help="A new directory for the source snapshot, reusable data cache and results",
    )
    parser.add_argument(
        "--godot",
        default="godot",
        help="Godot 4.7 editor executable, including inside Godot.app/Contents/MacOS",
    )
    parser.add_argument("--bun", default="bun")
    parser.add_argument(
        "--mode", choices=("realtime", "fixed", "metal"), default="realtime"
    )
    parser.add_argument(
        "--name",
        required=True,
        help="A new result subdirectory name, e.g. cold or warm",
    )
    arguments = parser.parse_args()
    if (
        not arguments.name
        or arguments.name in (".", "..")
        or Path(arguments.name).name != arguments.name
    ):
        parser.error("--name must be a single folder name")
    run(arguments)
