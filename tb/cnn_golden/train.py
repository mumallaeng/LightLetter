"""Training script for the confirmed model (C2-P1-S1-F3, FC 676->256->64->36).

Run (from LightLetter/tb):
    .venv/bin/python -u -m cnn_golden.train --data-root results/emnist \
      --output results/<run-name> --device mps

Re-running the same command skips a completed model and resumes an incomplete one
from its last saved epoch. Never run two training processes against the same output
path at once. A manifest mismatch (different source/environment/settings) is refused.
"""
import argparse
import hashlib
import json
import math
import time
from pathlib import Path

import numpy as np
import torch
from torch.nn import functional as F

from .data import load_split
from .model import Net

DATASET = 'ByClass-Uppercase-Digits'
SEED = 261014
EPOCHS = 10
BATCH_SIZE = 128
LR = .001


def batch(x, ids, device):
    # Converts raw uint8 pixels (0-255) to a float (0-1) training input on the device.
    return torch.from_numpy(np.asarray(x[ids, None], dtype=np.float32) / 255.).to(device)


@torch.no_grad()
def predict(net, x, device):
    net.eval()
    result = []
    for offset in range(0, len(x), BATCH_SIZE):
        result.append(net(batch(x, slice(offset, offset + BATCH_SIZE), device)).argmax(1).cpu().numpy())
    return np.concatenate(result)


def atomic_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
    temp.replace(path)


def scores(labels, predictions):
    # Confusion matrix rows are ground truth, columns are predictions. Balanced accuracy
    # is the mean per-class recall over all 36 classes.
    cm = np.bincount(labels * 36 + predictions, minlength=36 * 36).reshape(36, 36)
    return dict(accuracy=float(np.mean(labels == predictions)),
                digit_accuracy=float(np.mean(labels[labels < 10] == predictions[labels < 10])),
                letter_accuracy=float(np.mean(labels[labels >= 10] == predictions[labels >= 10])),
                balanced_accuracy=float(np.mean(cm.diagonal() / cm.sum(1))),
                correct=int((labels == predictions).sum()), count=len(labels), confusion=cm.tolist())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True,
                        help='Directory torchvision downloads/caches raw EMNIST files into.')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device', choices=['mps', 'cuda'], default='mps')
    parser.add_argument('--conv-init', choices=['default', 'center_identity'], default='center_identity')
    args = parser.parse_args()
    if not (torch.backends.mps.is_available() if args.device == 'mps' else torch.cuda.is_available()):
        raise RuntimeError('Requested GPU unavailable; CPU training fallback prohibited')
    torch.set_num_threads(4)
    args.output.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256((Path(__file__).read_bytes() +
                                   Path(__file__).with_name('model.py').read_bytes())).hexdigest()
    manifest = dict(model='C2-P1-S1-F3-fc676-256-64-36', dataset=DATASET, seed=SEED,
                    epochs=EPOCHS, batch_size=BATCH_SIZE, lr=LR, device=args.device,
                    conv_init=args.conv_init, loss_weighting='equal_digit_letter_group_mass',
                    source_sha256=source_hash, torch=torch.__version__)
    manifest_path = args.output / 'manifest.json'
    if manifest_path.exists() and json.loads(manifest_path.read_text()) != manifest:
        raise ValueError('Existing experiment protocol differs; use a new output directory')
    atomic_json(manifest_path, manifest)

    x, y = load_split(args.data_root, 'train')
    tx, ty = load_split(args.data_root, 'test')
    data_source = dict(loader='torchvision.datasets.EMNIST', split='byclass',
                       root=str(args.data_root))
    counts = np.bincount(y, minlength=36)
    if np.any(counts == 0):
        raise ValueError('Training data is missing a class')
    letter_weight = float(counts[:10].sum() / counts[10:].sum())
    class_weights = torch.ones(36, device=args.device)
    class_weights[10:] = letter_weight

    out = args.output / DATASET
    out.mkdir(parents=True, exist_ok=True)
    result_path = out / 'result.json'
    if result_path.exists() and json.loads(result_path.read_text()).get('status') == 'COMPLETE':
        print('COMPLETE (already)', DATASET, flush=True)
        return

    torch.manual_seed(SEED)
    net = Net(conv_init=args.conv_init).to(args.device)
    initial_conv_weights = [layer.weight.detach().clone() for layer in net.convs]
    optimizer = torch.optim.Adam(net.parameters(), lr=LR)
    history = []
    checkpoint = out / 'last.pt'
    previous = json.loads(result_path.read_text()) if result_path.exists() else {}
    if checkpoint.exists():
        # A resumed run restores the model, the Adam state, and the completed epochs.
        saved = torch.load(checkpoint, map_location=args.device, weights_only=False)
        net.load_state_dict(saved['model'])
        optimizer.load_state_dict(saved['optimizer'])
        history = saved['history']
    start = time.monotonic()
    record = dict(status='TRAINING', inventory=net.inventory(),
                  training_images=len(y), test_images=len(ty), data_source=data_source,
                  history=history, device=args.device,
                  conv_init=args.conv_init, letter_loss_weight=letter_weight,
                  first_batch_conv_gradient_l1=previous.get('first_batch_conv_gradient_l1'))
    atomic_json(result_path, record)

    for epoch in range(len(history), EPOCHS):
        net.train()
        order = np.random.default_rng(SEED + epoch).permutation(len(y))
        total = torch.zeros((), device=args.device)
        for offset in range(0, len(y), BATCH_SIZE):
            ids = order[offset:offset + BATCH_SIZE]
            target = torch.from_numpy(np.array(y[ids])).to(args.device)
            optimizer.zero_grad(set_to_none=True)
            loss = F.cross_entropy(net(batch(x, ids, args.device)), target, weight=class_weights)
            loss.backward()
            if epoch == 0 and offset == 0:
                # Abort immediately if every Conv layer's first-batch gradient is zero.
                norms = [float(layer.weight.grad.abs().sum().detach().cpu())
                         if layer.weight.grad is not None else 0. for layer in net.convs]
                record['first_batch_conv_gradient_l1'] = norms
                missing = [i + 1 for i, norm in enumerate(norms) if norm <= 0.]
                if missing:
                    raise RuntimeError(f'Conv layers without first-batch gradient: {missing}')
            optimizer.step()
            total += loss.detach() * len(ids)
        loss_value = float(total.cpu()) / len(y)
        if not math.isfinite(loss_value):
            raise RuntimeError('Nonfinite training loss')
        history.append(dict(epoch=epoch + 1, loss=loss_value,
                            session_seconds=round(time.monotonic() - start, 2)))
        # Snapshot the weight scale as of the last optimizer step. Test inputs are never used here.
        for layer, quant in zip([*net.convs, *net.fcs], net.weight_quant):
            quant(layer.weight)
        temporary = out / 'last.tmp'
        torch.save(dict(model=net.state_dict(), optimizer=optimizer.state_dict(), history=history), temporary)
        temporary.replace(checkpoint)
        record.update(history=history)
        atomic_json(result_path, record)
        print(DATASET, history[-1], flush=True)

    # Evaluate the same final-epoch weights with QAT simulation on and off.
    quantized = predict(net, tx, args.device)
    net.quantization(False)
    floating = predict(net, tx, args.device)
    conv_changes = [float((layer.weight.detach() - initial).abs().max().cpu())
                    for layer, initial in zip(net.convs, initial_conv_weights)]
    if any(change <= 0. for change in conv_changes):
        raise RuntimeError(f'Conv weight did not change from initialization: {conv_changes}')
    record.update(status='COMPLETE', qat16=scores(ty, quantized),
                  float_same_weights=scores(ty, floating),
                  conv_weight_max_delta_from_initial=conv_changes,
                  predicted_class_count=int(np.unique(quantized).size),
                  prediction_agreement=float(np.mean(floating == quantized)))
    np.savez_compressed(out / 'predictions.npz', labels=ty, qat16=quantized, floating=floating)
    atomic_json(result_path, record)
    print('COMPLETE', DATASET, record['qat16']['accuracy'], flush=True)


if __name__ == '__main__':
    main()
