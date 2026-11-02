"""Compare tiled model inference to a single padded inference (seam regression)."""
import argparse
import numpy as np
import torch
from spandrel import ModelLoader
from upscale import process

ap = argparse.ArgumentParser()
ap.add_argument('model')
args = ap.parse_args()
device = 'mps' if torch.backends.mps.is_available() else 'cpu'
model = ModelLoader().load_from_file(args.model).to(device).eval()
rgb = np.random.default_rng(410).integers(0,256,(71,67,3),dtype=np.uint8)
for boundary in ('wrap','reflect'):
    full = np.asarray(process(model,rgb,device,boundary,tile=256))
    tiled = np.asarray(process(model,rgb,device,boundary,tile=32))
    error = np.abs(full.astype(np.int16)-tiled.astype(np.int16))
    assert error.max() <= 1, (boundary,error.max())
    print(boundary, 'full versus tiled max RGB difference:', int(error.max()))
