"""Speech Commands v0.02 loading, splitting, and feature caching.

Uses the corpus's own validation_list.txt and testing_list.txt so the split
matches published keyword spotting results and a speaker never appears in two
splits. Twelve classes, the standard benchmark task: ten keywords plus silence
plus unknown.
"""

import hashlib
import wave as wavelib
from pathlib import Path

import numpy as np

from .features import FeatureConfig, MfccExtractor

KEYWORDS = ["yes", "no", "up", "down", "left", "right", "on", "off", "stop", "go"]
SILENCE = "_silence_"
UNKNOWN = "_unknown_"
LABELS = KEYWORDS + [SILENCE, UNKNOWN]
LABEL_INDEX = {name: i for i, name in enumerate(LABELS)}
BACKGROUND_DIR = "_background_noise_"


def read_wav(path: Path) -> np.ndarray:
    """Read a 16 bit mono PCM wav into float32 in [-1, 1]."""
    with wavelib.open(str(path), "rb") as w:
        frames = w.readframes(w.getnframes())
        data = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
        if w.getnchannels() > 1:
            data = data.reshape(-1, w.getnchannels()).mean(axis=1)
    return data


def split_of(relative_path: str, val_set: set, test_set: set) -> str:
    if relative_path in val_set:
        return "val"
    if relative_path in test_set:
        return "test"
    return "train"


def _which_set_for_silence(name: str) -> str:
    """Deterministic split for generated silence clips, stable across runs."""
    digest = hashlib.sha1(name.encode()).hexdigest()
    bucket = int(digest[:8], 16) % 100
    if bucket < 10:
        return "val"
    if bucket < 20:
        return "test"
    return "train"


class SpeechCommands:
    def __init__(self, root: Path, cfg: FeatureConfig = FeatureConfig(), seed: int = 0):
        self.root = Path(root)
        self.cfg = cfg
        self.extractor = MfccExtractor(cfg)
        self.rng = np.random.default_rng(seed)
        if not self.root.exists():
            raise FileNotFoundError(
                f"corpus not found at {self.root}, run model/fetch_dataset.sh"
            )

    def _lists(self):
        def load(name):
            path = self.root / name
            if not path.exists():
                return set()
            return set(path.read_text().split())

        return load("validation_list.txt"), load("testing_list.txt")

    def _background_clips(self):
        bg_dir = self.root / BACKGROUND_DIR
        if not bg_dir.exists():
            return []
        return [read_wav(p) for p in sorted(bg_dir.glob("*.wav"))]

    def build(self, unknown_ratio: float = 1.0, silence_ratio: float = 1.0):
        """Return {split: (waveforms, labels)} with waveforms as float32 clips.

        unknown_ratio and silence_ratio are multiples of the mean per keyword
        count, keeping the twelve classes roughly balanced.
        """
        val_set, test_set = self._lists()
        buckets = {s: {"wave": [], "label": []} for s in ("train", "val", "test")}
        unknown_pool = {s: [] for s in ("train", "val", "test")}

        word_dirs = sorted(
            d for d in self.root.iterdir() if d.is_dir() and d.name != BACKGROUND_DIR
        )
        for word_dir in word_dirs:
            word = word_dir.name
            is_keyword = word in KEYWORDS
            for wav_path in sorted(word_dir.glob("*.wav")):
                rel = f"{word}/{wav_path.name}"
                split = split_of(rel, val_set, test_set)
                if is_keyword:
                    buckets[split]["wave"].append(wav_path)
                    buckets[split]["label"].append(LABEL_INDEX[word])
                else:
                    unknown_pool[split].append(wav_path)

        backgrounds = self._background_clips()
        for split in buckets:
            n_keyword = len(buckets[split]["wave"])
            per_class = max(1, n_keyword // max(1, len(KEYWORDS)))

            pool = unknown_pool[split]
            take = min(len(pool), int(per_class * unknown_ratio))
            chosen = self.rng.choice(len(pool), size=take, replace=False) if pool else []
            for i in chosen:
                buckets[split]["wave"].append(pool[i])
                buckets[split]["label"].append(LABEL_INDEX[UNKNOWN])

            n_silence = int(per_class * silence_ratio)
            for i in range(n_silence):
                buckets[split]["wave"].append(("silence", split, i))
                buckets[split]["label"].append(LABEL_INDEX[SILENCE])

        self._backgrounds = backgrounds
        return buckets

    def materialise(self, entries, labels, augment: bool = False):
        """Turn path or silence tokens into waveforms, optionally augmented."""
        cfg = self.cfg
        out = np.zeros((len(entries), cfg.clip_samples), dtype=np.float32)
        for i, entry in enumerate(entries):
            if isinstance(entry, tuple):
                out[i] = self._make_silence()
                continue
            wave = read_wav(entry)
            if wave.size < cfg.clip_samples:
                pad = cfg.clip_samples - wave.size
                offset = self.rng.integers(0, pad + 1) if augment else 0
                wave = np.pad(wave, (offset, pad - offset))
            out[i] = wave[: cfg.clip_samples]
            if augment:
                out[i] = self._augment(out[i])
        return out, np.asarray(labels, dtype=np.int64)

    def _make_silence(self):
        cfg = self.cfg
        if not self._backgrounds:
            return (self.rng.normal(0, 1e-4, cfg.clip_samples)).astype(np.float32)
        clip = self._backgrounds[self.rng.integers(len(self._backgrounds))]
        if clip.size <= cfg.clip_samples:
            clip = np.pad(clip, (0, cfg.clip_samples - clip.size))
            start = 0
        else:
            start = self.rng.integers(0, clip.size - cfg.clip_samples)
        seg = clip[start : start + cfg.clip_samples]
        return (seg * self.rng.uniform(0.0, 0.35)).astype(np.float32)

    def _augment(self, wave):
        """Time shift up to 100 ms and mixed in background noise."""
        cfg = self.cfg
        shift = int(self.rng.integers(-1600, 1601))
        wave = np.roll(wave, shift)
        if shift > 0:
            wave[:shift] = 0.0
        elif shift < 0:
            wave[shift:] = 0.0
        if self._backgrounds and self.rng.random() < 0.8:
            noise = self._make_silence()
            wave = wave + noise * self.rng.uniform(0.0, 0.1)
        return np.clip(wave, -1.0, 1.0).astype(np.float32)

    def features(self, waves: np.ndarray) -> np.ndarray:
        """(N, clip) waveforms to (N, n_frames, n_mfcc) float32 features."""
        return np.stack([self.extractor.mfcc(w) for w in waves]).astype(np.float32)
