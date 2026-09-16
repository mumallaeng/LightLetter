"""확정 모델(C2-P1-S1-F3, FC 676->256->64->36) 학습 스크립트.

실행 (LightLetter/tb에서):
    .venv/bin/python -u -m cnn_golden.train --cache results/260913-full-data \
      --output results/<run-name> --device mps

같은 명령을 다시 실행하면 완료 모델은 건너뛰고 미완료면 마지막 저장 epoch부터 재개한다.
동일 출력 경로에서 두 학습 프로세스를 동시에 실행하면 안 된다. 소스·환경·조건 manifest가
다르면 재사용을 거부한다.
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

from .full_data import load_cache
from .model import Net

DATASET = 'ByClass-Uppercase-Digits'
SEED = 261014
EPOCHS = 10
BATCH_SIZE = 128
LR = .001


def batch(x, ids, device):
    # 원본 uint8 픽셀(0~255)을 학습 입력 float(0~1)로 바꿔 GPU에 올린다.
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
    # confusion 행=정답, 열=예측. balanced accuracy는 36개 class별 recall 평균이다.
    cm = np.bincount(labels * 36 + predictions, minlength=36 * 36).reshape(36, 36)
    return dict(accuracy=float(np.mean(labels == predictions)),
                digit_accuracy=float(np.mean(labels[labels < 10] == predictions[labels < 10])),
                letter_accuracy=float(np.mean(labels[labels >= 10] == predictions[labels >= 10])),
                balanced_accuracy=float(np.mean(cm.diagonal() / cm.sum(1))),
                correct=int((labels == predictions).sum()), count=len(labels), confusion=cm.tolist())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
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

    x, y, train_source = load_cache(args.cache, DATASET, 'train')
    tx, ty, test_source = load_cache(args.cache, DATASET, 'test')
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
        # 중간에 멈춘 경우 모델뿐 아니라 Adam 상태와 완료 epoch도 이어받는다.
        saved = torch.load(checkpoint, map_location=args.device, weights_only=False)
        net.load_state_dict(saved['model'])
        optimizer.load_state_dict(saved['optimizer'])
        history = saved['history']
    start = time.monotonic()
    record = dict(status='TRAINING', inventory=net.inventory(),
                  training_images=len(y), test_images=len(ty), train_source=train_source,
                  test_source=test_source, history=history, device=args.device,
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
                # 첫 batch부터 Conv 기울기가 전부 0이면 즉시 중단한다.
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
        # 마지막 optimizer 갱신 이후의 weight scale을 저장한다. test 입력은 사용하지 않는다.
        for layer, quant in zip([*net.convs, *net.fcs], net.weight_quant):
            quant(layer.weight)
        temporary = out / 'last.tmp'
        torch.save(dict(model=net.state_dict(), optimizer=optimizer.state_dict(), history=history), temporary)
        temporary.replace(checkpoint)
        record.update(history=history)
        atomic_json(result_path, record)
        print(DATASET, history[-1], flush=True)

    # 동일한 마지막-epoch 가중치로 QAT 모사 ON/OFF를 각각 평가한다.
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
