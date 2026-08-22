"""MFCC front end, the single source of truth for feature extraction.

The firmware computes the same features on device. To guarantee the two agree,
the mel filterbank, the window, and the DCT matrix are generated here and
emitted as C tables by export.py, so the device never recomputes them. Any
change to FeatureConfig must be followed by regenerating the C tables and
rerunning the host against device comparison described in docs/features.md.
"""

from dataclasses import dataclass, asdict

import numpy as np


@dataclass(frozen=True)
class FeatureConfig:
    """Mirrors KWS_TRON/audio/mfcc_config.h. Keep the two in sync."""

    sample_rate: int = 16000
    clip_samples: int = 16000       # 1 s of audio per classification
    frame_samples: int = 480        # 30 ms analysis frame
    frame_stride: int = 320         # 20 ms hop, gives 49 frames over 1 s
    n_fft: int = 512                # smallest power of two holding a frame
    n_mel: int = 40
    n_mfcc: int = 10
    mel_low_hz: float = 20.0
    mel_high_hz: float = 4000.0     # voice band, well inside the 8 kHz Nyquist
    log_floor: float = 1e-6         # clamps log of empty mel bins

    @property
    def n_frames(self) -> int:
        return 1 + (self.clip_samples - self.frame_samples) // self.frame_stride

    @property
    def n_bins(self) -> int:
        return self.n_fft // 2 + 1

    def as_dict(self) -> dict:
        d = asdict(self)
        d["n_frames"] = self.n_frames
        d["n_bins"] = self.n_bins
        return d


def hann_window(n: int) -> np.ndarray:
    """Periodic Hann, the convention used for spectral analysis."""
    k = np.arange(n, dtype=np.float64)
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * k / n)).astype(np.float32)


def hz_to_mel(f):
    return 2595.0 * np.log10(1.0 + np.asarray(f, dtype=np.float64) / 700.0)


def mel_to_hz(m):
    return 700.0 * (10.0 ** (np.asarray(m, dtype=np.float64) / 2595.0) - 1.0)


def mel_filterbank(cfg: FeatureConfig) -> np.ndarray:
    """Triangular mel filterbank, shape (n_mel, n_bins), rows sum to about 1.

    Slaney style triangles on a mel grid, area normalised so that wide high
    frequency filters do not dominate the log energies.
    """
    edges_mel = np.linspace(
        hz_to_mel(cfg.mel_low_hz), hz_to_mel(cfg.mel_high_hz), cfg.n_mel + 2
    )
    edges_hz = mel_to_hz(edges_mel)
    bin_hz = np.arange(cfg.n_bins, dtype=np.float64) * cfg.sample_rate / cfg.n_fft

    fb = np.zeros((cfg.n_mel, cfg.n_bins), dtype=np.float64)
    for m in range(cfg.n_mel):
        left, centre, right = edges_hz[m], edges_hz[m + 1], edges_hz[m + 2]
        rising = (bin_hz - left) / max(centre - left, 1e-9)
        falling = (right - bin_hz) / max(right - centre, 1e-9)
        fb[m] = np.clip(np.minimum(rising, falling), 0.0, None)
        area = fb[m].sum()
        if area > 0:
            fb[m] /= area
    return fb.astype(np.float32)


def dct_matrix(cfg: FeatureConfig) -> np.ndarray:
    """Orthonormal DCT-II, shape (n_mfcc, n_mel)."""
    n, k = np.arange(cfg.n_mel), np.arange(cfg.n_mfcc)[:, None]
    d = np.cos(np.pi * k * (2.0 * n + 1.0) / (2.0 * cfg.n_mel))
    d *= np.sqrt(2.0 / cfg.n_mel)
    d[0] *= np.sqrt(0.5)
    return d.astype(np.float32)


class MfccExtractor:
    """Computes the (n_frames, n_mfcc) feature grid the model consumes."""

    def __init__(self, cfg: FeatureConfig = FeatureConfig()):
        self.cfg = cfg
        self.window = hann_window(cfg.frame_samples)
        self.filterbank = mel_filterbank(cfg)
        self.dct = dct_matrix(cfg)

    def frames(self, wave: np.ndarray) -> np.ndarray:
        """Slice a clip into overlapping frames, zero padded or truncated."""
        cfg = self.cfg
        wave = np.asarray(wave, dtype=np.float32).reshape(-1)
        if wave.size < cfg.clip_samples:
            wave = np.pad(wave, (0, cfg.clip_samples - wave.size))
        wave = wave[: cfg.clip_samples]

        idx = (
            np.arange(cfg.frame_samples)[None, :]
            + cfg.frame_stride * np.arange(cfg.n_frames)[:, None]
        )
        return wave[idx]

    def __call__(self, wave: np.ndarray) -> np.ndarray:
        return self.mfcc(wave)

    def mfcc(self, wave: np.ndarray) -> np.ndarray:
        """Full pipeline, one clip in, (n_frames, n_mfcc) float32 out."""
        cfg = self.cfg
        framed = self.frames(wave) * self.window
        spectrum = np.fft.rfft(framed, n=cfg.n_fft, axis=-1)
        power = (spectrum.real ** 2 + spectrum.imag ** 2).astype(np.float32)
        mel = power @ self.filterbank.T
        log_mel = np.log(np.maximum(mel, cfg.log_floor), dtype=np.float32)
        return (log_mel @ self.dct.T).astype(np.float32)

    def mfcc_frame(self, frame: np.ndarray) -> np.ndarray:
        """Single frame path, mirrors exactly what T3 runs per frame on device."""
        cfg = self.cfg
        frame = np.asarray(frame, dtype=np.float32).reshape(-1)[: cfg.frame_samples]
        if frame.size < cfg.frame_samples:
            frame = np.pad(frame, (0, cfg.frame_samples - frame.size))
        spectrum = np.fft.rfft(frame * self.window, n=cfg.n_fft)
        power = (spectrum.real ** 2 + spectrum.imag ** 2).astype(np.float32)
        mel = self.filterbank @ power
        log_mel = np.log(np.maximum(mel, cfg.log_floor), dtype=np.float32)
        return (self.dct @ log_mel).astype(np.float32)
