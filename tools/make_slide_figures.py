"""Render the two figures the contest deck needs into docs/figures.

Both are built from the raw device capture of the path independence run, so
the slides carry measured numbers rather than redrawn ones.

    python tools/make_slide_figures.py
"""

import glob
import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[1]
OUT = REPO_ROOT / "docs" / "figures"

NAVY = "#1E2761"
ICE = "#7FA8D9"
AMBER = "#F6AE2D"
SLATE = "#5A6B8C"
INK = "#16203F"


def latest_capture() -> str:
    """The newest run that carries both a cost table and a decision trace.

    Selected by content rather than by folder name, so the figures follow the
    most recent run that can actually support them instead of a hard coded one
    that a later measurement has superseded.
    """
    hits = sorted(glob.glob(str(REPO_ROOT / "experiments" / "*" / "data" / "raw_capture.txt")))
    for path in reversed(hits):
        text = Path(path).read_text()
        if "BENCH_COST" in text and "BENCH_TRACE" in text:
            print(f"figures from {Path(path).parents[1].name}")
            return text
    raise SystemExit("no capture with a cost table and a decision trace under experiments/")


def layer_inversion(text: str):
    """Per layer INT8 against FP32, with the depthwise layers called out.

    This is the figure the whole contribution rests on: the four depthwise
    layers are the only ones where INT8 loses, which is why the optimum is a
    mixed configuration no single precision build can express.
    """
    rows = [(int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)))
            for m in re.finditer(r"BENCH_COST (\d+) (\S+) int8=(\d+) fp32=(\d+)", text)]
    rows.sort()
    names = [r[1] for r in rows]
    int8 = np.array([r[2] for r in rows]) / 1e6
    fp32 = np.array([r[3] for r in rows]) / 1e6

    fig, ax = plt.subplots(figsize=(10, 4.4))
    x = np.arange(len(rows))
    width = 0.38
    ax.bar(x - width / 2, int8, width, label="INT8", color=ICE)
    ax.bar(x + width / 2, fp32, width, label="FP32", color=NAVY)
    for i, name in enumerate(names):
        if name.startswith("dw"):
            ax.axvspan(i - 0.5, i + 0.5, color=AMBER, alpha=0.16, zorder=0)
            ax.annotate("INT8 slower", (i, max(int8[i], fp32[i]) + 0.25), ha="center",
                        fontsize=9, color="#8A6100", fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels(names)
    ax.set_ylabel("million cycles per layer")
    ax.set_title("Measured per layer cost on this silicon: the four depthwise layers invert",
                 fontsize=12, color=INK, fontweight="bold")
    ax.legend(frameon=False)
    ax.grid(alpha=0.25, axis="y")
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "layer_inversion.png", dpi=200)
    plt.close(fig)


def convergence(text: str):
    """The controller walk, demotions then promotions after the probe reset.

    Reaching the same mask from both extremes is what makes the operating
    point a property of the silicon rather than of the starting configuration.
    """
    rows = [(int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16),
             int(m.group(4)), int(m.group(5)), int(m.group(6)))
            for m in re.finditer(
                r"BENCH_TRACE (\d+) before=(\w+) after=(\w+) act=(\d+) over=(\d+) cycles=(\d+)", text)]
    rows.sort()
    # The trace runs on into the benchmark's pinned live windows, where the
    # controller is held and every row is the pinned configuration's cost. The
    # walk ends where the controller declared convergence, so the figure does.
    at = re.search(r"BENCH_CONVERGE converged=1 mask=\S+ at=(\d+)", text)
    if at:
        rows = [r for r in rows if r[0] <= int(at.group(1))]
    step = list(range(1, len(rows) + 1))
    ms = [r[5] / 250000 for r in rows]

    fig, ax = plt.subplots(figsize=(10, 4.4))
    ax.plot(step, ms, "-", color=SLATE, lw=1.6, zorder=2)
    for action, style in ((1, dict(s=70, color=NAVY, label="demote to INT8")),
                          (2, dict(s=90, color=AMBER, marker="^", label="promote to FP32")),
                          (0, dict(s=26, color="#B9C4D6", label="no change"))):
        pts = [(s, y) for s, y, r in zip(step, ms, rows) if r[3] == action]
        if pts:
            ax.scatter(*zip(*pts), zorder=3, **style)
    ax.axhline(120, color="#C0392B", ls="--", lw=1.4, zorder=1)
    ax.annotate("120 ms deadline", (len(step) * 0.62, 121.5), color="#C0392B",
                fontsize=10, fontweight="bold")

    probe = [i + 1 for i, r in enumerate(rows) if r[1] != 0 and r[2] == 0]
    if probe:
        ax.axvline(probe[0] + 0.5, color=SLATE, ls=":", lw=1.4)
        ax.annotate("restart from all INT8", (probe[0] + 0.9, max(ms) * 0.985),
                    fontsize=9, color=INK, fontweight="bold")
    ax.set_xlabel("controller decision")
    ax.set_ylabel("inference latency, ms")
    ax.set_title("The controller reaches the same operating point from both extremes",
                 fontsize=12, color=INK, fontweight="bold")
    ax.legend(frameon=False, loc="upper right")
    ax.grid(alpha=0.25)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT / "convergence.png", dpi=200)
    plt.close(fig)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    text = latest_capture()
    layer_inversion(text)
    convergence(text)
    print(f"written to {OUT}")


if __name__ == "__main__":
    main()
