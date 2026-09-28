"""Train the DS-CNN keyword model and record the run under experiments/."""

import argparse
import json
import time
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

from .dataset import LABELS, SpeechCommands
from .features import FeatureConfig
from .model import DSCNN, mask_trailing_frames, parameter_count

REPO_ROOT = Path(__file__).resolve().parents[2]

# Raw test waveforms kept in the feature cache for the device replay source.
RAW_CLIPS_KEPT = 16


def pick_device() -> torch.device:
    if torch.backends.mps.is_available():
        return torch.device("mps")
    if torch.cuda.is_available():
        return torch.device("cuda")
    return torch.device("cpu")


def build_features(root: Path, cfg: FeatureConfig, cache_dir: Path, seed: int):
    """Materialise waveforms and features for every split, with caching."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    tag = f"{cfg.n_frames}x{cfg.n_mfcc}_seed{seed}"
    cache = cache_dir / f"features_{tag}.npz"
    if cache.exists():
        blob = np.load(cache)
        return {
            split: (blob[f"{split}_x"], blob[f"{split}_y"])
            for split in ("train", "val", "test")
        }

    corpus = SpeechCommands(root, cfg, seed=seed)
    buckets = corpus.build()
    out, saveable = {}, {}
    for split, bucket in buckets.items():
        augment = split == "train"
        waves, labels = corpus.materialise(bucket["wave"], bucket["label"], augment)
        feats = corpus.features(waves)
        out[split] = (feats, labels)
        saveable[f"{split}_x"] = feats
        saveable[f"{split}_y"] = labels
        if split == "test":
            # A few raw clips travel with the cache for the replay source.
            saveable["test_raw"] = waves[:RAW_CLIPS_KEPT].astype(np.float32)
            saveable["test_raw_y"] = labels[:RAW_CLIPS_KEPT]
        print(f"  {split:5} {feats.shape[0]:6} clips -> {feats.shape}")
    np.savez_compressed(cache, **saveable)
    return out


def normalise(train_x, splits):
    """Standardise per MFCC coefficient using training statistics only."""
    mean = train_x.mean(axis=(0, 1), keepdims=True)
    std = train_x.std(axis=(0, 1), keepdims=True) + 1e-6
    return {k: ((x - mean) / std, y) for k, (x, y) in splits.items()}, mean, std


def evaluate(model, x, y, device, batch=512, active=None):
    model.eval()
    correct = 0
    with torch.no_grad():
        for i in range(0, len(x), batch):
            xb = torch.from_numpy(x[i : i + batch]).unsqueeze(1).to(device)
            yb = torch.from_numpy(y[i : i + batch]).to(device)
            if active is not None:
                a = torch.full((xb.shape[0],), active, device=device)
                xb = mask_trailing_frames(xb, a)
            correct += (model(xb).argmax(1) == yb).sum().item()
    return correct / len(x)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="model/datasets/speech_commands_v0.02")
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch", type=int, default=256)
    parser.add_argument("--lr", type=float, default=3e-3)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=4)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--min-active-frames", type=int, default=16)
    parser.add_argument("--slug", default="dscnn-train")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    cfg = FeatureConfig()
    device = pick_device()
    print(f"device {device}, feature grid {cfg.n_frames}x{cfg.n_mfcc}")

    root = (REPO_ROOT / args.root) if not Path(args.root).is_absolute() else Path(args.root)
    splits = build_features(root, cfg, root.parent / "cache", args.seed)
    splits, mean, std = normalise(splits["train"][0], splits)

    model = DSCNN(len(LABELS), args.channels, args.blocks, cfg).to(device)
    print(f"model parameters {parameter_count(model)}")

    train_x, train_y = splits["train"]
    optimiser = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    steps = args.epochs * max(1, len(train_x) // args.batch)
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimiser, args.lr, total_steps=steps)

    history = {"epoch": [], "loss": [], "val_acc": []}
    best_acc, best_state = 0.0, None
    started = time.time()

    for epoch in range(args.epochs):
        model.train()
        order = np.random.permutation(len(train_x))
        running, batches = 0.0, 0
        for i in range(0, len(order) - args.batch + 1, args.batch):
            idx = order[i : i + args.batch]
            xb = torch.from_numpy(train_x[idx]).unsqueeze(1).to(device)
            yb = torch.from_numpy(train_y[idx]).to(device)

            # Random active context so the model supports a shrunken window.
            active = torch.randint(
                args.min_active_frames, cfg.n_frames + 1, (xb.shape[0],), device=device
            )
            xb = mask_trailing_frames(xb, active)

            loss = F.cross_entropy(model(xb), yb, label_smoothing=0.1)
            optimiser.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimiser.step()
            if schedule.last_epoch < steps - 1:
                schedule.step()
            running += loss.item()
            batches += 1

        val_acc = evaluate(model, *splits["val"], device)
        history["epoch"].append(epoch)
        history["loss"].append(running / max(1, batches))
        history["val_acc"].append(val_acc)
        print(f"  epoch {epoch:3} loss {running / max(1, batches):.4f} val {val_acc:.4f}")
        if val_acc > best_acc:
            best_acc = val_acc
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}

    if best_state:
        model.load_state_dict(best_state)
    test_acc = evaluate(model, *splits["test"], device)
    elapsed = time.time() - started
    print(f"best val {best_acc:.4f}, test {test_acc:.4f}, {elapsed:.0f}s")

    # Accuracy against active context, the curve the controller trades along.
    context_curve = {}
    for active in range(args.min_active_frames, cfg.n_frames + 1, 4):
        context_curve[active] = evaluate(model, *splits["test"], device, active=active)
    context_curve[cfg.n_frames] = test_acc

    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    run_dir = REPO_ROOT / "experiments" / f"{stamp}_{args.slug}"
    (run_dir / "plots").mkdir(parents=True, exist_ok=True)

    torch.save(
        {
            "state_dict": model.state_dict(),
            "config": cfg.as_dict(),
            "labels": LABELS,
            "channels": args.channels,
            "blocks": args.blocks,
            "norm_mean": mean,
            "norm_std": std,
        },
        run_dir / "checkpoint.pt",
    )
    (run_dir / "config.json").write_text(
        json.dumps(
            {
                "args": vars(args),
                "feature_config": cfg.as_dict(),
                "labels": LABELS,
                "parameters": parameter_count(model),
                "device": str(device),
            },
            indent=2,
        )
    )
    (run_dir / "results.md").write_text(
        f"# {args.slug}\n\n"
        f"Best validation accuracy {best_acc:.4f}, test accuracy {test_acc:.4f}, "
        f"{parameter_count(model)} parameters, {elapsed:.0f} s on {device}.\n\n"
        f"Accuracy against active feature frames is in plots/context_curve.png, "
        f"it is the curve the T5 controller trades along when it shrinks the window.\n"
    )
    _write_plots(run_dir / "plots", history, context_curve)
    print(f"run written to {run_dir}")


def _write_plots(plot_dir: Path, history, context_curve):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(1, 2, figsize=(10, 4))
    ax[0].plot(history["epoch"], history["loss"])
    ax[0].set_xlabel("epoch"), ax[0].set_ylabel("training loss")
    ax[1].plot(history["epoch"], history["val_acc"])
    ax[1].set_xlabel("epoch"), ax[1].set_ylabel("validation accuracy")
    for a in ax:
        a.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(plot_dir / "training.png", dpi=140)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(6, 4))
    keys = sorted(context_curve)
    ax.plot(keys, [context_curve[k] for k in keys], marker="o")
    ax.set_xlabel("active feature frames")
    ax.set_ylabel("test accuracy")
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(plot_dir / "context_curve.png", dpi=140)
    plt.close(fig)


if __name__ == "__main__":
    main()
