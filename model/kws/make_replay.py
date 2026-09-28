"""Build the replay corpus from the test split, alternating keyword and silence."""

import argparse
from pathlib import Path

import numpy as np

from .dataset import LABELS, SILENCE, SpeechCommands
from .features import FeatureConfig

REPO_ROOT = Path(__file__).resolve().parents[2]


def build(root: Path, clips: int, seed: int, keywords: list):
    cfg = FeatureConfig()
    corpus = SpeechCommands(root, cfg, seed=seed)
    buckets = corpus.build()
    test = buckets["test"]

    by_label = {}
    for entry, label in zip(test["wave"], test["label"]):
        by_label.setdefault(label, []).append(entry)

    silence_index = LABELS.index(SILENCE)
    chosen, chosen_labels = [], []

    # Alternate keyword and silence so the gate is crossed repeatedly.
    wanted = []
    for i in range(clips):
        if i % 2 == 1:
            wanted.append(silence_index)
        else:
            wanted.append(LABELS.index(keywords[(i // 2) % len(keywords)]))

    rng = np.random.default_rng(seed)
    for label in wanted:
        pool = by_label.get(label, [])
        if not pool:
            raise SystemExit(f"no test clips for label {LABELS[label]}")
        chosen.append(pool[int(rng.integers(len(pool)))])
        chosen_labels.append(label)

    waves, labels = corpus.materialise(chosen, chosen_labels, augment=False)

    # Attenuate corpus silence so it is unambiguously quiescent.
    for i, label in enumerate(labels):
        if label == silence_index:
            waves[i] = waves[i] * 0.05

    return waves, labels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="model/datasets/speech_commands_v0.02")
    parser.add_argument("--out", default="model/datasets/cache/replay_clips.npz")
    parser.add_argument("--clips", type=int, default=6)
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument(
        "--keywords", default="down,yes,stop",
        help="keywords cycled through the non silence slots",
    )
    args = parser.parse_args()

    root = REPO_ROOT / args.root if not Path(args.root).is_absolute() else Path(args.root)
    waves, labels = build(root, args.clips, args.seed, args.keywords.split(","))

    out = REPO_ROOT / args.out if not Path(args.out).is_absolute() else Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(out, waves=waves.astype(np.float32), labels=labels)

    rms = [float(np.sqrt(np.mean(w.astype(np.float64) ** 2))) for w in waves]
    print(f"{len(waves)} clips -> {out}")
    for i, (label, r) in enumerate(zip(labels, rms)):
        print(f"  {i}: {LABELS[label]:<10} rms {r:.5f}")
    print(f"flash cost {waves.size * 2 / 1024:.1f} KB")


if __name__ == "__main__":
    main()
