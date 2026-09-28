"""Convert a trained checkpoint to the device layer format and verify it against the float model."""

import numpy as np
import torch

from .features import FeatureConfig
from .model import DSCNN
from .quantize import (
    INT8_MAX,
    INT8_MIN,
    CONV,
    DEPTHWISE,
    FULLY_CONNECTED,
    POINTWISE,
    Calibrator,
    LayerSpec,
    QuantTensor,
    activation_quant,
    bias_to_int32,
    compute_requant,
    fold_batchnorm,
    quantise_weights_per_channel,
    requantise_reference,
)


def flatten_graph(model: DSCNN, cfg: FeatureConfig):
    """Fold batch norm and return ordered LayerSpec skeletons with fp32 weights."""
    layers = []

    weight, bias = fold_batchnorm(
        model.stem.weight.detach().cpu().numpy(), model.bn_stem
    )
    in_h, in_w = cfg.n_frames, cfg.n_mfcc
    out_h = (in_h + 2 * 5 - 10) // 2 + 1
    out_w = (in_w + 2 * 1 - 4) // 2 + 1
    layers.append(
        LayerSpec(
            name="stem",
            kind=CONV,
            in_shape=(in_h, in_w, 1),
            out_shape=(out_h, out_w, model.channels),
            kernel=(10, 4),
            stride=(2, 2),
            padding=(5, 1),
            relu=True,
            weight_fp32=np.ascontiguousarray(weight.transpose(0, 2, 3, 1)),
            bias_fp32=bias,
        )
    )

    height, width = out_h, out_w
    for index, block in enumerate(model.blocks):
        weight, bias = fold_batchnorm(
            block.depthwise.weight.detach().cpu().numpy(), block.bn_depthwise
        )
        # torch depthwise is (C, 1, kh, kw); device wants (1, kh, kw, C)
        depthwise = np.ascontiguousarray(weight.transpose(1, 2, 3, 0))
        layers.append(
            LayerSpec(
                name=f"dw{index}",
                kind=DEPTHWISE,
                in_shape=(height, width, model.channels),
                out_shape=(height, width, model.channels),
                kernel=(3, 3),
                stride=(1, 1),
                padding=(1, 1),
                relu=True,
                weight_fp32=depthwise,
                bias_fp32=bias,
            )
        )

        weight, bias = fold_batchnorm(
            block.pointwise.weight.detach().cpu().numpy(), block.bn_pointwise
        )
        layers.append(
            LayerSpec(
                name=f"pw{index}",
                kind=POINTWISE,
                in_shape=(height, width, model.channels),
                out_shape=(height, width, model.channels),
                kernel=(1, 1),
                stride=(1, 1),
                padding=(0, 0),
                relu=True,
                weight_fp32=np.ascontiguousarray(weight.transpose(0, 2, 3, 1)),
                bias_fp32=bias,
            )
        )

    layers.append(
        LayerSpec(
            name="fc",
            kind=FULLY_CONNECTED,
            in_shape=(1, 1, model.channels),
            out_shape=(1, 1, model.classifier.out_features),
            relu=False,
            weight_fp32=model.classifier.weight.detach().cpu().numpy(),
            bias_fp32=model.classifier.bias.detach().cpu().numpy(),
        )
    )
    return layers


def _module_order(model: DSCNN):
    order = [("stem", model.bn_stem)]
    for index, block in enumerate(model.blocks):
        order.append((f"dw{index}", block.bn_depthwise))
        order.append((f"pw{index}", block.bn_pointwise))
    order.append(("fc", model.classifier))
    return order


def calibrate(model: DSCNN, features: np.ndarray, device, batch: int = 256):
    """Collect activation ranges over calibration data, post ReLU."""
    order = _module_order(model)
    calibrator = Calibrator(model, order)
    model.eval()
    with torch.no_grad():
        for start in range(0, len(features), batch):
            chunk = torch.from_numpy(features[start : start + batch])
            model(chunk.unsqueeze(1).to(device))
    calibrator.close()

    ranges = {}
    for name, _module in order:
        low, high = calibrator.ranges[name]
        if name != "fc":
            low = max(low, 0.0)  # ReLU follows every layer except the classifier
        ranges[name] = (low, high)
    return ranges


def assign_quantisation(layers, ranges, input_range):
    """Fix every boundary scale, so a runtime precision switch is meaning preserving."""
    current = activation_quant(*input_range)
    for layer in layers:
        low, high = ranges[layer.name]
        output_quant = activation_quant(low, high)
        weight_int8, weight_scales = quantise_weights_per_channel(
            layer.weight_fp32
            if layer.kind != DEPTHWISE
            else np.ascontiguousarray(layer.weight_fp32.transpose(3, 0, 1, 2))
        )
        if layer.kind == DEPTHWISE:
            weight_int8 = np.ascontiguousarray(weight_int8.transpose(1, 2, 3, 0))

        layer.weight_int8 = weight_int8
        layer.weight_scales = weight_scales
        layer.bias_int32 = bias_to_int32(layer.bias_fp32, current.scale, weight_scales)
        layer.multiplier, layer.shift = compute_requant(
            current.scale, weight_scales, output_quant.scale
        )
        layer.input_quant = current
        layer.output_quant = output_quant

        # Quantised ReLU clamps at the output zero point, not the byte zero.
        layer.activation_min = (
            output_quant.zero_point if layer.relu else INT8_MIN
        )
        layer.activation_max = INT8_MAX
        current = output_quant
    return layers


def _pad_nhwc(tensor, pad_h, pad_w, value=0):
    if pad_h == 0 and pad_w == 0:
        return tensor
    return np.pad(
        tensor,
        ((pad_h, pad_h), (pad_w, pad_w), (0, 0)),
        mode="constant",
        constant_values=value,
    )


def run_layer_int8(layer: LayerSpec, x_q: np.ndarray) -> np.ndarray:
    """Reference int8 layer, the exact arithmetic the C core must reproduce."""
    input_offset = -layer.input_quant.zero_point
    output_offset = layer.output_quant.zero_point
    x = x_q.astype(np.int32) + input_offset

    if layer.kind == FULLY_CONNECTED:
        acc = layer.weight_int8.astype(np.int32) @ x.reshape(-1)
        acc = acc + layer.bias_int32
        out = np.array(
            [
                requantise_reference(acc[i], int(layer.multiplier[i]), int(layer.shift[i]))
                for i in range(acc.size)
            ]
        )
        out = out + output_offset
        return np.clip(
            out, layer.activation_min, layer.activation_max
        ).astype(np.int8).reshape(1, 1, -1)

    out_h, out_w, out_c = layer.out_shape
    kh, kw = layer.kernel
    sh, sw = layer.stride
    padded = _pad_nhwc(x, layer.padding[0], layer.padding[1], value=0)
    result = np.zeros((out_h, out_w, out_c), dtype=np.int8)

    for oh in range(out_h):
        for ow in range(out_w):
            patch = padded[oh * sh : oh * sh + kh, ow * sw : ow * sw + kw, :]
            if layer.kind == DEPTHWISE:
                # weight (1, kh, kw, C)
                acc = (patch * layer.weight_int8[0].astype(np.int32)).sum(axis=(0, 1))
            else:
                # weight (out_c, kh, kw, in_c)
                acc = (
                    layer.weight_int8.astype(np.int32) * patch[None, ...]
                ).sum(axis=(1, 2, 3))
            acc = acc + layer.bias_int32
            requantised = np.array(
                [
                    requantise_reference(
                        acc[c], int(layer.multiplier[c]), int(layer.shift[c])
                    )
                    for c in range(out_c)
                ]
            )
            requantised = requantised + output_offset
            result[oh, ow] = np.clip(
                requantised, layer.activation_min, layer.activation_max
            ).astype(np.int8)
    return result


def run_layer_fp32(layer: LayerSpec, x: np.ndarray) -> np.ndarray:
    if layer.kind == FULLY_CONNECTED:
        out = layer.weight_fp32 @ x.reshape(-1) + layer.bias_fp32
        return (np.maximum(out, 0.0) if layer.relu else out).reshape(1, 1, -1)

    out_h, out_w, out_c = layer.out_shape
    kh, kw = layer.kernel
    sh, sw = layer.stride
    padded = _pad_nhwc(x, layer.padding[0], layer.padding[1], value=0.0)
    result = np.zeros((out_h, out_w, out_c), dtype=np.float32)
    for oh in range(out_h):
        for ow in range(out_w):
            patch = padded[oh * sh : oh * sh + kh, ow * sw : ow * sw + kw, :]
            if layer.kind == DEPTHWISE:
                acc = (patch * layer.weight_fp32[0]).sum(axis=(0, 1))
            else:
                acc = (layer.weight_fp32 * patch[None, ...]).sum(axis=(1, 2, 3))
            acc = acc + layer.bias_fp32
            result[oh, ow] = np.maximum(acc, 0.0) if layer.relu else acc
    return result


def run_reference(layers, features_2d: np.ndarray, input_quant: QuantTensor, precision):
    """Forward pass with a per layer precision list, converting at boundaries as T4 does."""
    x_q = input_quant.quantise(features_2d[..., None])
    state, mode = x_q, "int8"

    for index, layer in enumerate(layers):
        want = precision[index]
        if want == "int8" and mode == "fp32":
            state = layer.input_quant.quantise(state)
            mode = "int8"
        elif want == "fp32" and mode == "int8":
            state = layer.input_quant.dequantise(state)
            mode = "fp32"

        if layer.kind == FULLY_CONNECTED:
            # Global average pool feeds the classifier, exact enough in int8 since the zero point cancels.
            if mode == "int8":
                pooled = np.round(state.astype(np.float32).mean(axis=(0, 1), keepdims=True))
                state = np.clip(pooled, -128, 127).astype(np.int8)
            else:
                state = state.mean(axis=(0, 1), keepdims=True).astype(np.float32)

        state = (
            run_layer_int8(layer, state) if want == "int8" else run_layer_fp32(layer, state)
        )
        mode = want

    if mode == "int8":
        return layers[-1].output_quant.dequantise(state).reshape(-1)
    return np.asarray(state, dtype=np.float32).reshape(-1)

            # Global average pool feeds the classifier, exact in int8 since the zero point cancels.