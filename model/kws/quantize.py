"""Post training quantisation to the device layer format, INT8 and FP32 per layer, see docs/inference_core.md."""

from dataclasses import dataclass, field

import numpy as np
import torch
import torch.nn as nn

CONV = "conv"
DEPTHWISE = "depthwise"
POINTWISE = "pointwise"
FULLY_CONNECTED = "fully_connected"

INT8_MIN, INT8_MAX = -128, 127


@dataclass
class QuantTensor:
    """Asymmetric int8 activation quantisation, real = scale * (q - zero)."""

    scale: float
    zero_point: int

    def quantise(self, x: np.ndarray) -> np.ndarray:
        q = np.round(x / self.scale) + self.zero_point
        return np.clip(q, INT8_MIN, INT8_MAX).astype(np.int8)

    def dequantise(self, q: np.ndarray) -> np.ndarray:
        return (q.astype(np.float32) - self.zero_point) * self.scale


@dataclass
class LayerSpec:
    name: str
    kind: str
    in_shape: tuple
    out_shape: tuple
    kernel: tuple = (1, 1)
    stride: tuple = (1, 1)
    padding: tuple = (0, 0)
    relu: bool = True
    weight_fp32: np.ndarray = field(default=None, repr=False)
    bias_fp32: np.ndarray = field(default=None, repr=False)
    weight_int8: np.ndarray = field(default=None, repr=False)
    bias_int32: np.ndarray = field(default=None, repr=False)
    weight_scales: np.ndarray = field(default=None, repr=False)
    multiplier: np.ndarray = field(default=None, repr=False)
    shift: np.ndarray = field(default=None, repr=False)
    input_quant: QuantTensor = None
    output_quant: QuantTensor = None
    activation_min: int = INT8_MIN
    activation_max: int = INT8_MAX


def quantise_multiplier(real_multiplier: float):
    """Split M into (int32 M0, shift n) with M = M0 * 2^-31 * 2^n, the TFLite and CMSIS-NN convention."""
    if real_multiplier <= 0.0:
        return 0, 0
    significand, exponent = np.frexp(real_multiplier)  # M = significand * 2^exponent
    q = int(round(significand * (1 << 31)))
    if q == (1 << 31):
        q //= 2
        exponent += 1
    q = min(q, (1 << 31) - 1)
    return int(q), int(exponent)


def fold_batchnorm(conv_weight, bn):
    """Fold a BatchNorm2d into the convolution weight that precedes it."""
    gamma = bn.weight.detach().cpu().numpy()
    beta = bn.bias.detach().cpu().numpy()
    mean = bn.running_mean.detach().cpu().numpy()
    var = bn.running_var.detach().cpu().numpy()
    inv_std = 1.0 / np.sqrt(var + bn.eps)
    scale = gamma * inv_std
    weight = conv_weight * scale.reshape(-1, *([1] * (conv_weight.ndim - 1)))
    bias = beta - mean * scale
    return weight.astype(np.float32), bias.astype(np.float32)


def quantise_weights_per_channel(weight: np.ndarray):
    """Symmetric per output channel int8 weights, zero point is always zero."""
    flat = weight.reshape(weight.shape[0], -1)
    scales = np.abs(flat).max(axis=1) / 127.0
    scales = np.where(scales < 1e-12, 1e-12, scales).astype(np.float32)
    q = np.round(flat / scales[:, None])
    q = np.clip(q, INT8_MIN, INT8_MAX).astype(np.int8).reshape(weight.shape)
    return q, scales


def activation_quant(min_value: float, max_value: float) -> QuantTensor:
    """Asymmetric int8 range that always represents zero exactly."""
    min_value = float(min(min_value, 0.0))
    max_value = float(max(max_value, 0.0))
    if max_value - min_value < 1e-9:
        max_value = min_value + 1e-9
    scale = (max_value - min_value) / 255.0
    zero_point = int(round(INT8_MIN - min_value / scale))
    return QuantTensor(scale, int(np.clip(zero_point, INT8_MIN, INT8_MAX)))


class Calibrator:
    """Collects activation ranges by running the model over sample inputs."""

    def __init__(self, model: nn.Module, module_order):
        self.ranges = {}
        self.handles = []
        self.module_order = module_order
        for name, module in module_order:
            self.handles.append(
                module.register_forward_hook(self._hook_for(name))
            )

    def _hook_for(self, name):
        def hook(_module, _inputs, output):
            value = output.detach().float()
            low = float(value.min())
            high = float(value.max())
            if name in self.ranges:
                prev_low, prev_high = self.ranges[name]
                low, high = min(low, prev_low), max(high, prev_high)
            self.ranges[name] = (low, high)

        return hook

    def close(self):
        for handle in self.handles:
            handle.remove()
        self.handles = []


def bias_to_int32(bias_fp32, input_scale, weight_scales):
    """Bias shares the product scale, so its addition stays exact in int32."""
    scale = input_scale * weight_scales
    scale = np.where(scale < 1e-20, 1e-20, scale)
    return np.round(bias_fp32 / scale).astype(np.int32)


def compute_requant(input_scale, weight_scales, output_scale):
    multipliers, shifts = [], []
    for weight_scale in weight_scales:
        m, n = quantise_multiplier(float(input_scale * weight_scale / output_scale))
        multipliers.append(m)
        shifts.append(n)
    return np.asarray(multipliers, dtype=np.int32), np.asarray(shifts, dtype=np.int32)


def requantise_reference(acc: np.ndarray, multiplier: int, shift: int) -> np.ndarray:
    """Reference for the device requantisation, used to validate the C core."""
    acc = np.asarray(acc, dtype=np.int64)
    left = max(shift, 0)
    right = max(-shift, 0)
    acc = acc << left
    product = (acc * int(multiplier) + (1 << 30)) >> 31
    if right:
        rounding = 1 << (right - 1)
        product = (product + rounding) >> right
    return product
