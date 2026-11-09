"""Score a pad_response.gd run. The default command enforces the correction-order regression."""

import argparse
import csv
import gzip
import json
from collections import defaultdict
from pathlib import Path


def read_run(prefix):
    def open_part(ext):
        path = prefix.parent / (prefix.name + ext)
        return path.open() if path.is_file() else gzip.open(str(path) + ".gz", "rt")

    with open_part(".json") as f:
        trials = json.load(f)
    rows = defaultdict(list)
    with open_part(".csv") as f:
        for raw in csv.DictReader(f):
            row = {key: float(val) for key, val in raw.items()}
            rows[int(row["trial"])].append(row)
    if set(rows) != set(range(len(trials))):
        raise ValueError("Missing trial data")
    return [(cfg, rows[i]) for i, cfg in enumerate(trials)]


def score(data):
    groups = {}
    for cfg, rows in data:
        mph, initial, correction, sign, brake, duration = cfg
        r0 = min(rows, key=lambda r: abs(r["t"]))
        r2 = min(rows, key=lambda r: abs(r["t"] - 0.2))
        r3 = min(rows, key=lambda r: abs(r["t"] - 0.3))
        zero = next(
            (r["t"] for r in rows if r["t"] >= 0 and r["rackDeg"] * sign <= 0), None
        )
        groups[mph, initial, correction, sign, brake, duration] = {
            "yawAt200": sign * r2["yawRateDegS"],
            "headingChange300": sign * (r3["headingDeg"] - r0["headingDeg"]),
            "centerSeconds": zero,
            "contacts": min(r["contacts"] for r in rows),
        }
    results = []
    lag = []
    yaw = []
    asymmetry = []
    for key, result in groups.items():
        mph, initial, correction, sign, brake, duration = key
        opposite = groups[mph, initial, correction, -sign, brake, duration]
        asymmetry.append(abs(result["yawAt200"] - opposite["yawAt200"]))
        if correction < 0 and duration == 0:
            release = groups[mph, initial, 0.0, sign, brake, duration]
            lag.append(result["centerSeconds"] - release["centerSeconds"])
            yaw.append(result["yawAt200"] - release["yawAt200"])
        results.append(
            {
                "seedMph": mph,
                "heldInput": initial,
                "correction": correction,
                "mirror": sign,
                "brake": brake,
                "sweepTicks": duration,
                **result,
            }
        )
    return {
        "trials": len(results),
        "contacts": int(min(r["contacts"] for r in results)),
        "mirrorMaxYawDeltaDegS": max(asymmetry),
        "comparedCorrections": len(lag),
        "laterThanRelease": sum(v > 1e-5 for v in lag),
        "moreOldYawThanRelease": sum(v > 0.2 for v in yaw),
        "maxExtraUnwindMs": max(lag, default=0) * 1000,
        "cases": results,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--baseline",
        action="store_true",
        help="Report historical behavior without applying the new gate.",
    )
    args = parser.parse_args()
    report = score(read_run(args.prefix))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "cases"}, indent=2))
    if report["contacts"] != 4 or report["mirrorMaxYawDeltaDegS"] > 0.01:
        raise SystemExit("Contact/symmetry regression")
    if not args.baseline and (
        report["laterThanRelease"] or report["moreOldYawThanRelease"]
    ):
        raise SystemExit(
            "A correction returned more slowly or produced stronger old-direction yaw than letting go."
        )
