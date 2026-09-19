"""Confirmed model: LeNet-5 3x3_schedule, FC 400->120->84->36, INT16 QAT.

conv_channels=[1,6,16] (kernel 3x3, stride 1, no padding) -> ReLU -> MaxPool(2x2,
stride 2) after each Conv -> Flatten(5x5x16=400) -> FC1(400->120) -> ReLU ->
FC2(120->84) -> ReLU -> FC3(84->36). Reproduces LeNet-5's spatial reduction
schedule (28->26->13->11->5, matching LeCun 1998's 32x32/5x5 schedule) inside this
project's fixed 3x3 kernel, chosen after training and comparing 5 candidates
(1-6-16, 1-6-8-8 conv-3-layer, this 3x3 schedule, and two 32x32/5x5 LeNet-5
references) -- see cnn_golden/README.md for the comparison table and rationale.

Weights and activations are fake-quantized to INT16 (Quant16, self-observed power-of-two
scale). Biases follow the standard integer-only-inference scheme instead (Jacob et al.
2017 / TFLite's quantization spec): each layer's bias_scale is *derived* as
input_scale * weight_scale (not independently observed), stored at INT32 (wider than
weight/activation) so it lines up with the accumulator's native fixed-point scale --
that's what lets an RTL accumulator add bias in with a plain integer add, no rescale.
This simulates integer boundaries, but MACs are computed in floating point, so results
are not bit-exact with RTL.
"""
import math

import torch
from torch import nn
from torch.nn import functional as F

CONV_CHANNELS = [1, 6, 16]
PADDING = 0
POOL_STRIDE = 2
FC_WIDTHS = [120, 84, 36]


def fake_quantize(x, scale, bits):
    """Rounds x onto the symmetric signed integer grid at `scale` (straight-through grad)."""
    limit = 2. ** (bits - 1)
    clipped = (x / scale).clamp(-limit, limit - 1)
    rounded = clipped + (clipped.round() - clipped).detach()
    return rounded * scale


class Quant16(nn.Module):
    """Fake-quantizes into the symmetric signed INT16 range.

    The scale is a power of two, rounding is round-to-even, and the saturated region's
    backward pass uses a straight-through estimator. The activation observer tracks the
    running max over training data; the weight observer is re-measured on every update.
    GPU only -- this never falls back to CPU training.
    """
    def __init__(self, weight=False):
        super().__init__()
        self.weight = weight
        self.enabled = True
        self.register_buffer('maximum', torch.tensor(0.))

    @property
    def scale(self):
        return 2. ** torch.ceil(torch.log2(self.maximum.clamp_min(1e-12) / 32767.))

    def forward(self, x):
        if not self.enabled:
            return x
        if self.training:
            with torch.no_grad():
                maximum = x.detach().abs().amax()
                self.maximum.copy_(maximum if self.weight else torch.maximum(self.maximum, maximum))
        return fake_quantize(x, self.scale.detach(), bits=16)


class Net(nn.Module):
    """Hardcodes the confirmed spec: LeNet-5 3x3_schedule, FC 400->120->84->36."""

    def __init__(self):
        super().__init__()
        self.convs = nn.ModuleList([
            nn.Conv2d(CONV_CHANNELS[i], CONV_CHANNELS[i + 1], 3, padding=PADDING)
            for i in range(len(CONV_CHANNELS) - 1)
        ])
        size = 28
        self.sizes = []
        for _ in self.convs:
            # Side length after a 3x3/stride-1 Conv: size-2+2*padding.
            size = size - 2 + 2 * PADDING
            # Side length after a 2x2 MaxPool at POOL_STRIDE; floor rule.
            size = (size - 2) // POOL_STRIDE + 1
            if size < 1:
                raise ValueError('Empty spatial output')
            self.sizes.append(size)
        widths = [CONV_CHANNELS[-1] * size * size] + FC_WIDTHS
        self.fcs = nn.ModuleList([nn.Linear(a, b) for a, b in zip(widths, widths[1:])])
        self.input_quant = Quant16()
        self.weight_quant = nn.ModuleList([Quant16(weight=True) for _ in [*self.convs, *self.fcs]])
        self.activation_quant = nn.ModuleList([Quant16() for _ in [*self.convs, *self.fcs]])
        self.bias_quant_enabled = True

    def quantization(self, enabled):
        for module in self.modules():
            if isinstance(module, Quant16):
                module.enabled = enabled
        self.bias_quant_enabled = enabled

    def _input_scale(self, i):
        # Scale of whatever quantizer actually produced layer i's input: input_quant for
        # the first conv, otherwise the previous layer's activation_quant.
        return self.input_quant.scale if i == 0 else self.activation_quant[i - 1].scale

    def _quantized_bias(self, i, bias):
        if not self.bias_quant_enabled:
            return bias
        bias_scale = (self._input_scale(i) * self.weight_quant[i].scale).detach()
        return fake_quantize(bias, bias_scale, bits=32)

    def forward(self, x):
        x = self.input_quant(x)
        for i, conv in enumerate(self.convs):
            x = F.conv2d(x, self.weight_quant[i](conv.weight), self._quantized_bias(i, conv.bias),
                        padding=PADDING)
            x = self.activation_quant[i](F.relu(x))
            x = F.max_pool2d(x, 2, POOL_STRIDE)
        x = x.flatten(1)
        for j, fc in enumerate(self.fcs):
            i = len(self.convs) + j
            x = F.linear(x, self.weight_quant[i](fc.weight), self._quantized_bias(i, fc.bias))
            if j < len(self.fcs) - 1:
                x = F.relu(x)
            # No ReLU on the last FC. The argmax over the 36 logits is the class ID.
            x = self.activation_quant[i](x)
        return x

    def inventory(self):
        weights = sum(m.weight.numel() for m in [*self.convs, *self.fcs])
        biases = sum(m.bias.numel() for m in [*self.convs, *self.fcs])
        return dict(weights=weights, biases=biases, weight_int16_bytes=weights * 2,
                    bias_int32_bytes=biases * 4,
                    weight_only_bram36_x18=math.ceil(weights / 2048),
                    spatial_outputs=self.sizes,
                    caveat='Weight-only packed BRAM lower bound; biases are INT32-quantized '
                           '(derived scale = input_scale x weight_scale, see bias_int32_bytes) '
                           'but not folded into this count; excludes activations, accumulators, '
                           'banking and control.')
