"""Build the replay corpus the device streams through its capture path.

The corpus decides what the pipeline can demonstrate. A run of identical
keywords exercises the classifier and nothing else: the voice activity gate
never closes, so the branch that skips feature extraction and inference on
silence, which is where the power saving comes from, is never entered.

This picks a stratified set that alternates keyword and silence, so a run
crosses the gate in both directions and the quiescent path is exercised as much
as the active one. Clips come from the corpus test split, so they are audio the
model was never trained on.

    python -m kws.make_replay --clips 6
"""

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

    # Alternate keyword and silence so the gate is crossed repeatedly rather
    # than once. An odd clip count ends on a keyword, which is harmless.
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

    # Silence from the corpus is background noise at a random level, which can
    # be loud enough to hold the gate open. Attenuate it so it is unambiguously
    # quiescent, which is what makes the gate's behaviour readable.
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
