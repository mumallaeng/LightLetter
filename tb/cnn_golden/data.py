"""Loads EMNIST digits+uppercase (36 classes) via torchvision, no team-specific files.

torchvision.datasets.EMNIST(split="byclass") ships 62 classes ordered 0-9 (digits),
10-35 (uppercase A-Z), 36-61 (lowercase a-z) -- see the EMNIST byclass mapping. We
keep labels 0-35 as-is and drop lowercase; no relabeling is needed.

torchvision's EMNIST images are stored flipped and rotated 90 degrees anticlockwise
relative to the human-readable orientation (a long-standing artifact of the original
NIST-to-EMNIST conversion, not a deliberate multi-angle augmentation -- see
https://github.com/pytorch/vision/issues/8783). A plain 2D transpose undoes it.
Skipping this step trains on sideways, mirrored characters without any error, so the
correction is verified indirectly by matching the previously confirmed accuracy.
"""
import numpy as np
from torchvision.datasets import EMNIST

NUM_CLASSES = 36


def load_split(root, partition):
    """Returns (images, labels) as uint8 (N,28,28) / int64 (N,), upright orientation."""
    dataset = EMNIST(root=str(root), split="byclass", train=(partition == "train"), download=True)
    images = dataset.data.numpy()
    labels = dataset.targets.numpy()
    images = np.transpose(images, (0, 2, 1))  # undoes the EMNIST flip+rotate artifact
    keep = labels < NUM_CLASSES
    return images[keep].copy(), labels[keep].astype(np.int64).copy()
