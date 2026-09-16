"""팀 CSV에 나열된 모든 PNG를 읽고 검증된 NumPy 캐시를 만든다.

원본 ZIP/CSV는 읽기 전용이다. 캐시는 CSV 순서 그대로이며 일부 표본만 고르지 않는다.
완료 metadata가 없으면 미완료 캐시로 간주하고 재사용하지 않는다.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile

import numpy as np
from PIL import Image

from .data import sha256

DATASETS = ("Letters+Digits", "ByClass-Uppercase-Digits")


def cache_partition(root, cache, configuration, partition):
    # 팀 제공 CSV의 각 행이 어느 ZIP의 어느 PNG를 가리키는지 그대로 따른다.
    # train/test 및 36-class mapping을 확인한 뒤 캐시를 만든다.
    base = Path(root)/configuration
    target = Path(cache)/configuration/partition
    csv_path = base/"labels"/(partition+".csv")
    manifest_path = base/"manifest.json"
    manifest = json.loads(manifest_path.read_text())
    with (base/"labels/classes.csv").open(newline="",encoding="utf-8-sig") as f:
        mapping = list(csv.DictReader(f))
    if [int(r["class_id"]) for r in mapping] != list(range(36)):
        raise ValueError("Expected 36-class contiguous mapping")
    identity = dict(csv_sha256=sha256(csv_path), manifest_sha256=sha256(manifest_path),
                    mapping=mapping, configuration=configuration, partition=partition)
    if (target/"complete.json").exists():
        # 완료 표시가 있는 캐시도 출처와 실제 배열 파일 해시를 다시 검사한다.
        saved = json.loads((target/"complete.json").read_text())
        if any(saved.get(k)!=v for k,v in identity.items()):
            raise ValueError("Cache provenance mismatch")
        for filename in ("images.npy","labels.npy"):
            if sha256(target/filename) != saved[filename+"_sha256"]:
                raise ValueError("Cache payload hash mismatch")
        return saved
    target.mkdir(parents=True,exist_ok=False)
    expected = manifest["partitions"][partition]
    n = expected["images"]
    images = np.lib.format.open_memmap(target/"images.npy",mode="w+",dtype=np.uint8,shape=(n,28,28))
    labels = np.lib.format.open_memmap(target/"labels.npy",mode="w+",dtype=np.int64,shape=(n,))
    counts = np.zeros(36,dtype=np.int64)
    digest = hashlib.sha256()
    archive_name,archive = None,None
    rows = 0
    try:
        with csv_path.open(newline="",encoding="utf-8-sig") as f:
            for i,row in enumerate(csv.DictReader(f)):
                # 다른 partition으로 빠지는 경로나 ZIP 바깥 경로를 받아들이지 않는다.
                path = PurePosixPath(row["image_path"])
                name = row["image_archive"]
                c = int(row["class_id"])
                if (i>=n or not 0<=c<36 or path.is_absolute() or ".." in path.parts
                        or path.parts[0]!=partition or Path(name).name!=name
                        or not name.startswith(partition+"-")):
                    raise ValueError("Invalid CSV row/partition")
                if name != archive_name:
                    if archive is not None:
                        archive.close()
                    archive = zipfile.ZipFile(base/"images"/name)
                    archive_name = name
                raw = archive.read(str(path))
                with Image.open(io.BytesIO(raw)) as im:
                    # 이 데이터는 이미 바로 세운 28×28 grayscale이다.
                    # 여기서 회전/전치를 추가하면 정답과 입력 방향이 어긋난다.
                    if im.mode!="L" or im.size!=(28,28):
                        raise ValueError("Expected upright grayscale 28x28")
                    images[i] = np.asarray(im)
                labels[i] = c
                counts[c] += 1
                digest.update(hashlib.sha256(raw).digest())
                rows = i+1
                if rows % 50000 == 0:
                    print(f"CACHE {configuration}/{partition}: {rows}/{n}",flush=True)
    finally:
        if archive is not None:
            archive.close()
    if rows!=n or any(counts[c]!=expected["class_counts"][str(c)] for c in range(36)):
        raise ValueError("Full dataset counts disagree with manifest")
    images.flush()
    labels.flush()
    saved = dict(**identity,images=n,class_counts=counts.tolist(),
                 ordered_png_digest=digest.hexdigest(),orientation="upright_no_transform",
                 source_zip_sha256_declared=manifest["source_zip_sha256"])
    for filename in ("images.npy","labels.npy"):
        saved[filename+"_sha256"] = sha256(target/filename)
    (target/"complete.json").write_text(json.dumps(saved,ensure_ascii=False,indent=2)+"\n")
    print(f"CACHE COMPLETE {configuration}/{partition}: {n}",flush=True)
    return saved


def load_cache(cache,configuration,partition):
    # 학습에서는 전체 배열을 한꺼번에 float로 복사하지 않고 필요한 batch만 읽는다.
    base = Path(cache)/configuration/partition
    meta = json.loads((base/"complete.json").read_text())
    # Copy-on-write mmap avoids modifying cache files or allocating all float images.
    x = np.load(base/"images.npy",mmap_mode="c",allow_pickle=False)
    y = np.load(base/"labels.npy",mmap_mode="c",allow_pickle=False)
    if x.shape!=(meta["images"],28,28) or y.shape!=(len(x),):
        raise ValueError("Cache shape mismatch")
    return x,y,meta


def split_train(labels,seed=261013,fraction=.05):
    # 이전 학습 실험용 train/validation 분할 함수다.
    # 현재 qat_sweep.py는 이 함수를 호출하지 않고 train 전체를 매 epoch 사용한다.
    rng = np.random.default_rng(seed)
    train,valid = [],[]
    for c in range(36):
        ids = np.flatnonzero(labels==c)
        rng.shuffle(ids)
        count = max(1,int(round(len(ids)*fraction)))
        valid.extend(ids[:count])
        train.extend(ids[count:])
    return np.array(train,dtype=np.int64),np.array(valid,dtype=np.int64)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data-root",type=Path,required=True)
    p.add_argument("--cache",type=Path,required=True)
    args = p.parse_args()
    for name in DATASETS:
        for part in ("train","test"):
            cache_partition(args.data_root,args.cache,name,part)


if __name__=="__main__":
    main()
