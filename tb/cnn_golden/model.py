"""Confirmed model: C2-P1-S1, FC 676->256->64->36, INT16 QAT.

Conv x2 (3x3, zero-padding 1, stride 1, 1 output channel) -> ReLU -> MaxPool(2x2,
stride 1) after each Conv -> Flatten(26x26=676) -> FC1(676->256) -> ReLU ->
FC2(256->64) -> ReLU -> FC3(64->36). This hardcodes the single configuration chosen
after sweeping 16 structures and comparing FC widths. The sweep history and width
comparison code live in Vault/projects/LightLetter/sweep-exploration/, not this repo.

This simulates INT16 boundaries, but MACs are computed in floating point, so results
are not bit-exact with RTL.
"""
import math

import torch
from torch import nn
from torch.nn import functional as F

CONV_LAYERS = 2
PADDING = 1
POOL_STRIDE = 1
FC_WIDTHS = [256, 64, 36]


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
        scale = self.scale.detach()
        clipped = (x / scale).clamp(-32768, 32767)
        rounded = clipped + (clipped.round() - clipped).detach()
        return rounded * scale


class Net(nn.Module):
    """Hardcodes the confirmed spec: C2-P1-S1-F3, FC 676->256->64->36."""

    def __init__(self, conv_init='center_identity'):
        super().__init__()
        # "1 filter per layer" is implemented as 1 output channel; the input is 1-channel grayscale.
        self.convs = nn.ModuleList([nn.Conv2d(1, 1, 3, padding=PADDING) for _ in range(CONV_LAYERS)])
        if conv_init == 'center_identity':
            # Prevents the single output channel from dying behind an initial negative bias.
            with torch.no_grad():
                for conv in self.convs:
                    conv.weight.zero_()
                    conv.weight[0, 0, 1, 1] = 1.
                    conv.bias.zero_()
        elif conv_init != 'default':
            raise ValueError(f'Unknown Conv initialization: {conv_init}')
        size = 28
        self.sizes = []
        for _ in self.convs:
            # Side length after a 3x3/stride-1 Conv: size-2+2*padding.
            size = size - 2 + 2 * PADDING
            # Side length after a 2x2 MaxPool; stride 1 uses the floor rule.
            size = (size - 2) // POOL_STRIDE + 1
            if size < 1:
                raise ValueError('Empty spatial output')
            self.sizes.append(size)
        widths = [size * size] + FC_WIDTHS
        self.fcs = nn.ModuleList([nn.Linear(a, b) for a, b in zip(widths, widths[1:])])
        self.input_quant = Quant16()
        self.weight_quant = nn.ModuleList([Quant16(weight=True) for _ in [*self.convs, *self.fcs]])
        self.activation_quant = nn.ModuleList([Quant16() for _ in [*self.convs, *self.fcs]])

    def quantization(self, enabled):
        for module in self.modules():
            if isinstance(module, Quant16):
                module.enabled = enabled

    def forward(self, x):
        x = self.input_quant(x)
        for i, conv in enumerate(self.convs):
            x = F.conv2d(x, self.weight_quant[i](conv.weight), conv.bias, padding=PADDING)
            x = self.activation_quant[i](F.relu(x))
            x = F.max_pool2d(x, 2, POOL_STRIDE)
        x = x.flatten(1)
        for j, fc in enumerate(self.fcs):
            i = len(self.convs) + j
            x = F.linear(x, self.weight_quant[i](fc.weight), fc.bias)
            if j < len(self.fcs) - 1:
                x = F.relu(x)
            # No ReLU on the last FC. The argmax over the 36 logits is the class ID.
            x = self.activation_quant[i](x)
        return x

    def inventory(self):
        weights = sum(m.weight.numel() for m in [*self.convs, *self.fcs])
        biases = sum(m.bias.numel() for m in [*self.convs, *self.fcs])
        return dict(weights=weights, biases=biases, weight_int16_bytes=weights * 2,
                    weight_only_bram36_x18=math.ceil(weights / 2048),
                    spatial_outputs=self.sizes,
                    caveat='Weight-only packed BRAM lower bound; excludes biases, activations, accumulators, banking and control.')
