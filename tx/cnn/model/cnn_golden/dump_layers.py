"""Writes the weight + per-layer dump that the RTL export and the UI read, from a train.py run.

Run (from tx/cnn/model, after cnn_golden.train has completed):
    .venv/bin/python -m cnn_golden.dump_layers --data-root results/emnist \
      --run results/<run-name>

Loads the best-validation-epoch weights from <run>/<DATASET>/last.pt, re-checks test accuracy
against result.json, and writes cnn_golden/results/layer_outputs/lenet5_3x3_schedule.json
(weights plus every stage's output for one test image). The JSON format matches what
cnn_golden.ipynb wrote, so tx/cnn/golden/export_*.py read it unchanged.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import torch
from torch.nn import functional as F

from .data import CLASS_NAMES, NUM_CLASSES, load_split
from .model import PADDING, POOL_STRIDE, Net
from .train import DATASET, predict, scores

OUT_PATH = Path(__file__).with_name('results') / 'layer_outputs' / 'lenet5_3x3_schedule.json'
SAMPLE_INDEX = 0


def inspect_layers(net, image):
    """image: (28, 28) uint8. Same order as Net.forward, one entry per stage."""
    net.eval()
    stages = []
    with torch.no_grad():
        x = torch.from_numpy(np.asarray(image[None, None], dtype=np.float32) / 255.)
        x = net.input_quant(x)
        stages.append(('input (quantized)', x.clone()))
        for i, conv in enumerate(net.convs):
            x = F.conv2d(x, net.weight_quant[i](conv.weight), net._quantized_bias(i, conv.bias), padding=PADDING)
            stages.append((f'conv{i + 1} (raw)', x.clone()))
            x = net.activation_quant[i](F.relu(x))
            stages.append((f'conv{i + 1} + ReLU (quantized)', x.clone()))
            x = F.max_pool2d(x, 2, POOL_STRIDE)
            stages.append((f'conv{i + 1} + MaxPool', x.clone()))
        x = x.flatten(1)
        stages.append(('flatten', x.clone()))
        for j, fc in enumerate(net.fcs):
            i = len(net.convs) + j
            x = F.linear(x, net.weight_quant[i](fc.weight), net._quantized_bias(i, fc.bias))
            stages.append((f'fc{j + 1} (raw)', x.clone()))
            if j < len(net.fcs) - 1:
                x = F.relu(x)
            x = net.activation_quant[i](x)
            stages.append((f'fc{j + 1} (quantized' + ('+ReLU)' if j < len(net.fcs) - 1 else ')'), x.clone()))
    return stages


def weights_to_jsonable(net):
    """Raw weight, the quantized weight forward uses, and bias, in net.convs + net.fcs order."""
    net.eval()
    entries = []
    names = [f'conv{i + 1}' for i in range(len(net.convs))] + [f'fc{j + 1}' for j in range(len(net.fcs))]
    with torch.no_grad():
        for name, layer, wq in zip(names, [*net.convs, *net.fcs], net.weight_quant):
            entries.append(dict(layer=name,
                                weight_shape=list(layer.weight.shape),
                                weight_raw=layer.weight.tolist(),
                                weight_quantized=wq(layer.weight).tolist(),
                                bias_shape=list(layer.bias.shape),
                                bias=layer.bias.tolist()))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True)
    parser.add_argument('--run', type=Path, required=True, help='train.py --output directory')
    parser.add_argument('--out', type=Path, default=OUT_PATH)
    args = parser.parse_args()

    result = json.loads((args.run / DATASET / 'result.json').read_text())
    if result.get('status') != 'COMPLETE':
        raise RuntimeError(f'{args.run / DATASET} has not finished training')
    saved = torch.load(args.run / DATASET / 'last.pt', map_location='cpu', weights_only=False)
    net = Net()
    net.load_state_dict(saved['best_model'])
    net.eval()

    x_test, y_test = load_split(args.data_root, 'test')
    trained = result['best_val_epoch_test_qat16']['accuracy']
    reloaded = scores(y_test, predict(net, x_test, 'cpu'))['accuracy']
    print(f'test accuracy: train.py {trained:.4f} | reloaded {reloaded:.4f}')
    if abs(trained - reloaded) > 1e-3:
        raise RuntimeError('Reloaded weights do not reproduce the train.py test accuracy')

    sample_image, sample_label = x_test[SAMPLE_INDEX], int(y_test[SAMPLE_INDEX])
    stages = inspect_layers(net, sample_image)
    pred = int(stages[-1][1].argmax())
    print(f'sample test[{SAMPLE_INDEX}] label={CLASS_NAMES[sample_label]} pred={CLASS_NAMES[pred]}')

    payload = dict(model='lenet5_3x3_schedule', num_classes=NUM_CLASSES, class_names=CLASS_NAMES,
                   source=dict(run=str(args.run), best_epoch=result['best_epoch'], test_accuracy_qat16=trained),
                   sample_label=sample_label, sample_label_name=CLASS_NAMES[sample_label],
                   weights=weights_to_jsonable(net),
                   stages=[dict(name=n, shape=list(t.shape), values=t.squeeze(0).tolist()) for n, t in stages])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2))
    print(f'wrote {args.out} ({args.out.stat().st_size / 1e6:.1f} MB, '
          f'{len(payload["weights"])} weight layers, {len(stages)} stages)')


if __name__ == '__main__':
    main()
