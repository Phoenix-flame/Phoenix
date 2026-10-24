"""Generate parallax height maps for Sponza from its normal maps.

The glTF has no height maps, so each normal map's slopes are integrated into a height
field (Frankot-Chellappa: a least-squares integration done in the Fourier domain, which
suits these tiling textures), high-passed so broad undulations don't use up the parallax
depth, and saved beside it as "<normal map>_height.jpg" (white = high). The engine picks
these up automatically (see FindHeightBeside in phoenix/src/renderer/Mesh.cpp).

Only opaque tiling materials get one (their UVs span more than one repeat: walls, floor,
bricks, cloth). One-off textures such as the lion relief or the columns sit on geometry
that is already modelled, and their atlas islands would warp under parallax; cut-out
foliage would have its outline shift.

Usage:  python3 make_height_maps.py [Sponza.gltf]     (needs numpy and Pillow)
"""
import json
import os
import sys

import numpy as np
from PIL import Image


def height_from_normals(path, highpass_px=48.0):
    n = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0 * 2.0 - 1.0
    nx, ny, nz = n[..., 0], n[..., 1], np.maximum(n[..., 2], 0.1)
    # Slopes along image columns (x, right) and rows (y, down). The normal maps are
    # glTF/OpenGL style: +Y points up the image, so a positive ny climbs against the rows.
    p = -nx / nz
    q = ny / nz
    rows, cols = p.shape
    wx = 2.0 * np.pi * np.fft.fftfreq(cols)[None, :]
    wy = 2.0 * np.pi * np.fft.fftfreq(rows)[:, None]
    denom = wx ** 2 + wy ** 2
    denom[0, 0] = 1.0
    h_hat = (-1j * wx * np.fft.fft2(p) - 1j * wy * np.fft.fft2(q)) / denom
    h_hat[0, 0] = 0.0
    # High-pass: subtract a Gaussian blur of about highpass_px pixels.
    h_hat *= 1.0 - np.exp(-0.5 * denom * highpass_px ** 2)
    h = np.real(np.fft.ifft2(h_hat))
    lo, hi = np.percentile(h, [1.0, 99.5])
    return np.clip((h - lo) / max(hi - lo, 1e-9), 0.0, 1.0)


def uv_extent(gltf, folder, material_index):
    """Largest UV span over the primitives using this material."""
    buffers = {}
    extent = 0.0
    for mesh in gltf["meshes"]:
        for prim in mesh["primitives"]:
            if prim.get("material") != material_index or "TEXCOORD_0" not in prim["attributes"]:
                continue
            accessor = gltf["accessors"][prim["attributes"]["TEXCOORD_0"]]
            view = gltf["bufferViews"][accessor["bufferView"]]
            if view["buffer"] not in buffers:
                uri = gltf["buffers"][view["buffer"]]["uri"]
                with open(os.path.join(folder, uri), "rb") as f:
                    buffers[view["buffer"]] = f.read()
            stride = view.get("byteStride", 8) // 4
            start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            data = np.frombuffer(buffers[view["buffer"]], dtype=np.float32, offset=start,
                                 count=(accessor["count"] - 1) * stride + 2)
            uv = np.stack([data[0::stride], data[1::stride]], axis=1)
            extent = max(extent, float(np.max(uv.max(axis=0) - uv.min(axis=0))))
    return extent


def main():
    gltf_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "Sponza.gltf")
    folder = os.path.dirname(os.path.abspath(gltf_path))
    with open(gltf_path) as f:
        gltf = json.load(f)
    done = set()
    for index, material in enumerate(gltf["materials"]):
        if "normalTexture" not in material or material.get("alphaMode") == "MASK":
            continue
        if uv_extent(gltf, folder, index) < 1.5:
            continue
        texture = gltf["textures"][material["normalTexture"]["index"]]
        uri = gltf["images"][texture["source"]]["uri"]
        if uri in done:
            continue
        done.add(uri)
        stem = os.path.splitext(uri)[0]
        out = os.path.join(folder, stem + "_height.jpg")
        height = height_from_normals(os.path.join(folder, uri))
        Image.fromarray((height * 255.0 + 0.5).astype(np.uint8), "L").save(out, quality=90)
        print("wrote", os.path.basename(out))


if __name__ == "__main__":
    main()
