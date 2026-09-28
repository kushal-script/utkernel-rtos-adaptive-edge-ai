"""Turn the device benchmark report into an experiment folder with figures."""

import argparse
import json
import re
import sys
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

RUN_RE = re.compile(
    r"BENCH (?P<name>\S+) mask=(?P<mask>\w+) n=(?P<n>\d+) acc_ppm=(?P<acc>\d+) "
    r"mean=(?P<mean>\d+) worst=(?P<worst>\d+) best=(?P<best>\d+) us=(?P<us>\d+)"
)
LAYER_RE = re.compile(r"BENCH_LAYER (?P<name>\S+) (?P<index>\d+) (?P<layer>\S+) (?P<cycles>\d+)")
STATE_RE = re.compile(
    r"BENCH_(?P<kind>STATE|CONTROL|MEMORY|CONVERGE|PIPELINE|CAPTURE|POWER|GRID) (?P<body>.*)"
)
LIVE_RE = re.compile(
    r"BENCH_LIVE (?P<name>\S+) mask=(?P<mask>\w+) ms=(?P<ms>\d+) inferences=(?P<inf>\d+) "
    r"scored=(?P<scored>\d+) correct=(?P<correct>\d+) idle_ppm=(?P<idle>\d+) "
    r"blocks=(?P<blocks>\d+) overruns=(?P<ov>\d+)"
)


def read_serial(port: str, baud: int = 115200, timeout: float = 120.0,
                idle_limit: float = 90.0) -> str:
    import serial

    lines = []
    started = False
    with serial.Serial(port, baud, timeout=1) as handle:
        print(f"listening on {port} for BENCH_BEGIN, up to {timeout:.0f} s")
        # Separate allowances for the start and for silence between lines, a live window is 30 s quiet.
        quiet = timeout
        while quiet > 0:
            raw = handle.readline()
            if not raw:
                quiet -= 1
                continue
            text = raw.decode("utf-8", errors="replace").rstrip()
            if "BENCH_BEGIN" in text:
                started = True
            if started:
                lines.append(text)
                print(" ", text)
                quiet = idle_limit
            if "BENCH_END" in text:
                break
        else:
            if started:
                print(f"warning: no output for {idle_limit:.0f} s and no "
                      f"BENCH_END, the capture below is incomplete")
            else:
                print(f"warning: no BENCH_BEGIN within {timeout:.0f} s")
    return "\n".join(lines)


def parse(text: str) -> dict:
    runs, layers, state, live = {}, {}, {}, {}

    for match in RUN_RE.finditer(text):
        data = match.groupdict()
        runs[data["name"]] = {
            "mask": data["mask"],
            "samples": int(data["n"]),
            "accuracy": int(data["acc"]) / 1e6,
            "mean_cycles": int(data["mean"]),
            "worst_cycles": int(data["worst"]),
            "best_cycles": int(data["best"]),
            "mean_us": int(data["us"]),
        }

    for match in LIVE_RE.finditer(text):
        d = match.groupdict()
        scored = int(d["scored"])
        live[d["name"]] = {
            "mask": d["mask"],
            "window_ms": int(d["ms"]),
            "inferences": int(d["inf"]),
            "scored": scored,
            "correct": int(d["correct"]),
            "accuracy": (int(d["correct"]) / scored) if scored else None,
            "idle_fraction": int(d["idle"]) / 1e6,
            "blocks": int(d["blocks"]),
            "overruns": int(d["ov"]),
        }

    for match in LAYER_RE.finditer(text):
        data = match.groupdict()
        layers.setdefault(data["name"], []).append(
            {"index": int(data["index"]), "layer": data["layer"],
             "cycles": int(data["cycles"])}
        )

    for match in STATE_RE.finditer(text):
        kind = match.group("kind").lower()
        fields = {}
        for token in match.group("body").split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = int(value) if value.isdigit() else value
        state[kind] = fields

    for name in layers:
        layers[name].sort(key=lambda row: row["index"])
    return {"runs": runs, "layers": layers, "state": state, "live": live}


def write_plots(plot_dir: Path, parsed: dict):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    runs = parsed["runs"]
    if not runs:
        return

    names = list(runs)
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))

    ax[0].bar(names, [runs[n]["mean_us"] for n in names])
    for index, name in enumerate(names):
        ax[0].plot(index, runs[name]["worst_cycles"] / runs[name]["mean_cycles"]
                   * runs[name]["mean_us"], marker="_", markersize=28, color="black")
    ax[0].set_ylabel("microseconds per inference")
    ax[0].set_title("latency, bar is mean and tick is worst case")

    ax[1].bar(names, [runs[n]["accuracy"] * 100 for n in names])
    ax[1].set_ylabel("accuracy, percent")
    ax[1].set_ylim(0, 100)
    ax[1].set_title("accuracy on the on device evaluation set")
    for a in ax:
        a.grid(alpha=0.3, axis="y")
    fig.tight_layout()
    fig.savefig(plot_dir / "latency_accuracy.png", dpi=140)
    plt.close(fig)

    if parsed.get("live"):
        names = list(parsed["live"])
        fig, ax = plt.subplots(1, 2, figsize=(11, 4))

        ax[0].bar(names, [parsed["live"][n]["idle_fraction"] * 100 for n in names])
        ax[0].set_ylabel("core idle, percent of wall time")
        ax[0].set_title("energy proxy, whole pipeline live")

        # Wilson intervals, the end to end sample is small.
        import math

        centres, errs = [], [[], []]
        for n in names:
            row = parsed["live"][n]
            k, total = row["correct"], row["scored"]
            if not total:
                centres.append(0.0)
                errs[0].append(0.0)
                errs[1].append(0.0)
                continue
            p_hat, z = k / total, 1.96
            denom = 1 + z * z / total
            centre = (p_hat + z * z / (2 * total)) / denom
            half = z * math.sqrt(p_hat * (1 - p_hat) / total +
                                 z * z / (4 * total * total)) / denom
            centres.append(p_hat * 100)
            errs[0].append(max(0.0, (p_hat - (centre - half)) * 100))
            errs[1].append(max(0.0, ((centre + half) - p_hat) * 100))

        ax[1].bar(names, centres, yerr=errs, capsize=5)
        ax[1].set_ylabel("end to end accuracy, percent")
        ax[1].set_ylim(0, 100)
        ax[1].set_title("end to end accuracy, 95 percent interval")
        for a in ax:
            a.grid(alpha=0.3, axis="y")
        fig.tight_layout()
        fig.savefig(plot_dir / "live_energy_accuracy.png", dpi=140)
        plt.close(fig)

    if parsed["layers"]:
        fig, ax = plt.subplots(figsize=(9, 4))
        width = 0.8 / max(1, len(parsed["layers"]))
        for offset, (name, rows) in enumerate(parsed["layers"].items()):
            positions = [row["index"] + offset * width for row in rows]
            ax.bar(positions, [row["cycles"] for row in rows], width=width, label=name)
        first = next(iter(parsed["layers"].values()))
        ax.set_xticks([row["index"] + 0.4 for row in first])
        ax.set_xticklabels([row["layer"] for row in first], rotation=45, ha="right")
        ax.set_ylabel("cycles per layer, mean")
        ax.legend()
        ax.grid(alpha=0.3, axis="y")
        fig.tight_layout()
        fig.savefig(plot_dir / "layer_cycles.png", dpi=140)
        plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", help="serial port the board is on")
    parser.add_argument("--file", help="read a captured report instead of a port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--slug", default="bench")
    args = parser.parse_args()

    if args.file:
        text = Path(args.file).read_text()
    elif args.port:
        text = read_serial(args.port, args.baud)
    else:
        text = sys.stdin.read()

    parsed = parse(text)
    if not parsed["runs"]:
        print("no BENCH lines found, nothing to record")
        return 1

    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    run_dir = REPO_ROOT / "experiments" / f"{stamp}_{args.slug}"
    (run_dir / "plots").mkdir(parents=True, exist_ok=True)
    (run_dir / "data").mkdir(parents=True, exist_ok=True)

    (run_dir / "data" / "report.txt").write_text(text)
    (run_dir / "config.json").write_text(json.dumps(parsed, indent=2))
    write_plots(run_dir / "plots", parsed)

    lines = ["# Benchmark run", ""]
    lines.append("| configuration | accuracy | mean us | worst cycles |")
    lines.append("| :-- | --: | --: | --: |")
    for name, run in parsed["runs"].items():
        lines.append(
            f"| {name} | {run['accuracy'] * 100:.1f} | {run['mean_us']} | "
            f"{run['worst_cycles']} |"
        )
    if "memory" in parsed["state"]:
        lines.append("")
        lines.append(f"Peak layer pool use {parsed['state']['memory'].get('peak_pool_bytes')} bytes.")
    (run_dir / "results.md").write_text("\n".join(lines) + "\n")

    for name, run in parsed["runs"].items():
        print(f"{name:10} acc {run['accuracy'] * 100:5.1f}%  mean {run['mean_us']:6} us  "
              f"worst {run['worst_cycles']:9} cycles")
    print(f"written to {run_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
