"""팀이 제공한 EMNIST PNG ZIP을 압축 해제나 원본 변경 없이 읽는다."""
import csv
import hashlib
import io
import json
import random
import zipfile
from contextlib import ExitStack
from pathlib import Path, PurePosixPath

import numpy as np
from PIL import Image

CONFIGURATIONS = ("ByClass", "ByMerge", "Letters+Digits", "ByClass-Uppercase-Digits")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_samples(root, configuration, partition="test", per_class=1, seed=261008):
    if configuration not in CONFIGURATIONS or partition not in ("train", "test"):
        raise ValueError("Unknown configuration or partition")
    if per_class < 1:
        raise ValueError("per_class must be positive")
    base = Path(root) / configuration
    manifest = json.loads((base / "manifest.json").read_text())
    with (base / "labels/classes.csv").open(newline="", encoding="utf-8-sig") as f:
        mapping = list(csv.DictReader(f))
    count = int(manifest["class_count"])
    if [int(row["class_id"]) for row in mapping] != list(range(count)):
        raise ValueError("Class mapping is not contiguous or disagrees with manifest")
    # Stratified reservoir sampling: deterministic, covers all classes, scans only CSV.
    rng, buckets, seen = random.Random(seed), [[] for _ in range(count)], [0] * count
    csv_path = base / "labels" / f"{partition}.csv"
    with csv_path.open(newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            c = int(row["class_id"])
            if not 0 <= c < count:
                raise ValueError("Out-of-range class ID")
            image_path = PurePosixPath(row["image_path"])
            archive = row["image_archive"]
            if (image_path.is_absolute() or ".." in image_path.parts
                    or image_path.parts[0] != partition or Path(archive).name != archive
                    or not archive.startswith(partition + "-")):
                raise ValueError("Invalid partition/archive/image path")
            seen[c] += 1
            if len(buckets[c]) < per_class:
                buckets[c].append(row)
            else:
                j = rng.randrange(seen[c])
                if j < per_class:
                    buckets[c][j] = row
    expected = manifest["partitions"][partition]
    if sum(seen) != expected["images"] or any(
            seen[c] != expected["class_counts"][str(c)] for c in range(count)):
        raise ValueError("CSV counts disagree with manifest")
    if any(len(b) < per_class for b in buckets):
        raise ValueError("Requested more images per class than available")
    rows = [row for bucket in buckets for row in bucket]
    images = []
    with ExitStack() as stack:
        archives = {}
        for row in rows:
            archive = row["image_archive"]
            if archive not in archives:
                archives[archive] = stack.enter_context(zipfile.ZipFile(base / "images" / archive))
            raw = archives[archive].read(row["image_path"])
            with Image.open(io.BytesIO(raw)) as im:
                if im.mode != "L" or im.size != (28, 28):
                    raise ValueError("Expected upright 28x28 grayscale PNG")
                images.append(np.asarray(im).copy())
            row["png_sha256"] = hashlib.sha256(raw).hexdigest()
    evidence = dict(configuration=configuration, partition=partition, classes=count,
                    per_class=per_class, seed=seed, orientation="upright_no_transform",
                    source_zip_sha256_declared=manifest["source_zip_sha256"],
                    manifest_sha256=sha256(base / "manifest.json"),
                    labels_sha256=sha256(csv_path), mapping=mapping, samples=rows)
    return np.stack(images), np.array([int(r["class_id"]) for r in rows]), evidence
