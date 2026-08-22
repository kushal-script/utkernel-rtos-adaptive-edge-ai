"""Depthwise separable CNN for keyword spotting.

Follows the DS-CNN topology from ARM's Hello Edge work, which is the standard
reference point for keyword spotting accuracy on Cortex-M, so the numbers this
project reports can be compared against published ones. Batch norm is folded
into the preceding convolution at export time, so the deployed graph is only
convolution, depthwise convolution, pointwise convolution, ReLU, average pool,
and one fully connected layer. That keeps the hand written device side core
small and makes per layer precision switching tractable.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F

from .features import FeatureConfig


class DepthwiseSeparableBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.depthwise = nn.Conv2d(
            channels, channels, kernel_size=3, padding=1, groups=channels, bias=False
        )
        self.bn_depthwise = nn.BatchNorm2d(channels)
        self.pointwise = nn.Conv2d(channels, channels, kernel_size=1, bias=False)
        self.bn_pointwise = nn.BatchNorm2d(channels)

    def forward(self, x):
        x = F.relu(self.bn_depthwise(self.depthwise(x)))
        return F.relu(self.bn_pointwise(self.pointwise(x)))


class DSCNN(nn.Module):
    """Input (N, 1, n_frames, n_mfcc), output logits over n_classes."""

    def __init__(
        self,
        n_classes: int = 12,
        channels: int = 64,
        n_blocks: int = 4,
        cfg: FeatureConfig = FeatureConfig(),
    ):
        super().__init__()
        self.cfg = cfg
        self.channels = channels
        self.n_blocks = n_blocks

        self.stem = nn.Conv2d(
            1, channels, kernel_size=(10, 4), stride=(2, 2), padding=(5, 1), bias=False
        )
        self.bn_stem = nn.BatchNorm2d(channels)
        self.blocks = nn.ModuleList(
            DepthwiseSeparableBlock(channels) for _ in range(n_blocks)
        )
        self.classifier = nn.Linear(channels, n_classes)

    def forward(self, x):
        x = F.relu(self.bn_stem(self.stem(x)))
        for block in self.blocks:
            x = block(x)
        x = x.mean(dim=(2, 3))
        return self.classifier(x)


def mask_trailing_frames(features: torch.Tensor, active: torch.Tensor) -> torch.Tensor:
    """Zero every frame at or beyond `active`, per sample.

    The device computes only `active` feature frames when the controller
    shrinks the window, and leaves the rest of the grid at zero. Training with
    the same masking is what makes that a supported operating point rather than
    feeding the model an input distribution it never saw. See docs/adaptation.md.
    """
    n_frames = features.shape[-2]
    index = torch.arange(n_frames, device=features.device).view(1, 1, n_frames, 1)
    return features * (index < active.view(-1, 1, 1, 1)).to(features.dtype)


def parameter_count(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())
