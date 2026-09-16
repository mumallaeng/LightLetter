"""확정 모델: C2-P1-S1, FC 676->256->64->36, INT16 QAT.

Conv 2층(3x3, zero-padding 1, stride 1, 출력 채널 1) -> 각 Conv 뒤 ReLU -> MaxPool(2x2,
stride 1) -> Flatten(26x26=676) -> FC1(676->256) -> ReLU -> FC2(256->64) -> ReLU ->
FC3(64->36). 16개 구조를 비교한 스윕과 FC 폭 비교 실험에서 골라낸 단일 확정 구성을
하드코딩한다. 스윕 이력·다른 폭 비교 코드는 Vault/projects/LightLetter/sweep-exploration/에
있다(이 repo에는 없음).

INT16 경계는 모사하지만 MAC은 부동소수점으로 계산하므로 RTL의 비트 단위 결과는 아니다.
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
    """대칭 signed INT16 범위의 가짜 양자화를 적용한다.

    scale은 2의 거듭제곱, 반올림은 짝수 쪽, 포화 구간의 역전파는 STE를 사용한다.
    활성값 관측기는 학습 데이터의 누적 최댓값을 쓰고 가중치는 갱신 때마다 다시 관측한다.
    GPU 연산만 사용하며 CPU 학습으로 자동 전환하지 않는다.
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
    """확정 스펙(C2-P1-S1-F3, FC 676->256->64->36)을 하드코딩한 모델."""

    def __init__(self, conv_init='center_identity'):
        super().__init__()
        # '층당 필터 1개'를 출력 채널 1개로 구현했다. 입력도 grayscale 1채널이다.
        self.convs = nn.ModuleList([nn.Conv2d(1, 1, 3, padding=PADDING) for _ in range(CONV_LAYERS)])
        if conv_init == 'center_identity':
            # 단일 출력 채널이 초기 음수 bias로 모두 꺼지는 현상을 방지한다.
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
            # 3x3/stride 1 Conv 후 한 변 길이: size-2+2*padding.
            size = size - 2 + 2 * PADDING
            # 2x2 MaxPool 후 한 변 길이. stride 1은 floor 규칙을 쓴다.
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
            # 마지막 FC에는 ReLU를 두지 않는다. 36개 logit의 최대 index가 class ID다.
            x = self.activation_quant[i](x)
        return x

    def inventory(self):
        weights = sum(m.weight.numel() for m in [*self.convs, *self.fcs])
        biases = sum(m.bias.numel() for m in [*self.convs, *self.fcs])
        return dict(weights=weights, biases=biases, weight_int16_bytes=weights * 2,
                    weight_only_bram36_x18=math.ceil(weights / 2048),
                    spatial_outputs=self.sizes,
                    caveat='Weight-only packed BRAM lower bound; excludes biases, activations, accumulators, banking and control.')
