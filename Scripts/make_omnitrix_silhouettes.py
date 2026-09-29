"""
Alien Museum - fills in the Omnitrix dial's silhouettes (system Python 3 with numpy and Pillow).

    python Scripts/make_omnitrix_silhouettes.py

Reads SourceArt/Omnitrix/<AlienId>.tri (the flattened triangles Scripts/omnitrix_silhouettes.py dump wrote)
and writes SourceArt/Omnitrix/Silhouettes/<AlienId>.png: the alien's shape in white on black, 256 x 256,
upright (head at the top), centred and fitted inside the dial's circle. Drawn 4x larger and scaled down,
so the edges are smooth. Then run omnitrix_silhouettes.py import.
"""
import glob
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SOURCE = r"C:/Games/Ben10/SourceArt/Omnitrix"
OUT = os.path.join(SOURCE, "Silhouettes")
SIZE = 256
SUPER = 4
RADIUS = 0.42  # the shape stays within this share of the image width from the centre: inside the dial
MIN_FILL = 0.12  # thinner shapes (Wildvine's tendrils, Stinkfly's legs) are thickened until they read on the watch


def draw(path):
    corners = np.fromfile(path, dtype="<f4").reshape(-1, 3, 2)
    points = corners.reshape(-1, 2)
    lo, hi = points.min(0), points.max(0)
    centre = (lo + hi) * 0.5
    reach = np.sqrt(((points - centre) ** 2).sum(1)).max()
    big = SIZE * SUPER
    scale = RADIUS * big / max(reach, 1e-6)
    pixels = (corners - centre) * scale
    pixels[..., 1] *= -1.0  # image rows run down
    pixels += big * 0.5
    image = Image.new("L", (big, big), 0)
    pen = ImageDraw.Draw(image)
    for triangle in pixels:
        pen.polygon([tuple(p) for p in triangle], fill=255)
    small = image.resize((SIZE, SIZE), Image.LANCZOS)
    # A spindly shape all but vanishes on a 3 cm face: thicken it a little at a time.
    for _ in range(5):
        if np.asarray(small).mean() / 255.0 >= MIN_FILL:
            break
        image = image.filter(ImageFilter.MaxFilter(7))
        small = image.resize((SIZE, SIZE), Image.LANCZOS)
    return small, len(corners)


def main():
    os.makedirs(OUT, exist_ok=True)
    paths = sorted(glob.glob(os.path.join(SOURCE, "*.tri")))
    if not paths:
        raise SystemExit(f"no .tri files in {SOURCE}: run omnitrix_silhouettes.py dump first")
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        image, count = draw(path)
        image.save(os.path.join(OUT, f"{name}.png"))
        print(f"{name}: {count} triangles -> {name}.png ({100.0 * np.asarray(image).mean() / 255.0:.0f}% filled)")


if __name__ == "__main__":
    main()
