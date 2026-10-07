# Makes the installer art from the source images in media\images.
#
# The source images are the art as it was drawn. This script only changes the
# size and the format, so that a redraw needs one command and no hand work:
#
#     python bwvr/tools/make_art.py
#
# It writes into bwvr/installer/art, which is what the installer and the
# launcher read. It never writes into media\images.
import os
import struct
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(REPO, 'media', 'images')
OUT = os.path.join(REPO, 'bwvr', 'installer', 'art')

# The side image of the first and last pages of setup. Inno Setup keeps the
# aspect ratio, so the source is fitted to exactly this size.
SIDE = ('Header-410x785.jpg', 'wizard-side.png', (410, 785))
# The small image at the top right of every other page, shown at about 55 px.
SMALL = ('Black & White VR Logo - Black.jpg', 'wizard-small.png', (138, 138))
# The icon of the launcher, the setup program and every shortcut.
ICON_SOURCE = 'Black & White VR Logo - Black.jpg'
ICON_SIZES = [16, 24, 32, 48, 64, 128, 256]


def load(name):
    path = os.path.join(SOURCE, name)
    if not os.path.exists(path):
        raise SystemExit('missing source image: ' + path)
    return Image.open(path).convert('RGBA')


def write_png(name, out_name, size):
    image = load(name)
    # LANCZOS keeps the letters clean when the picture is made smaller.
    image = image.resize(size, Image.LANCZOS)
    path = os.path.join(OUT, out_name)
    image.save(path, 'PNG', optimize=True)
    print('%-20s %4d x %-4d  <- %s' % (out_name, size[0], size[1], name))


def trimmed(image, margin=0.05, floor=28):
    """The mark, without the flat border around it, kept square.

    The logo is drawn with a wide margin. At 16 px that margin is most of the
    icon, and the letters turn to mush. Cutting it back is only a crop, so the
    art itself is unchanged."""
    grey = image.convert('L').point(lambda v: 255 if v > floor else 0)
    box = grey.getbbox()
    if box is None:
        return image
    left, top, right, bottom = box
    cx = (left + right) / 2.0
    cy = (top + bottom) / 2.0
    half = max(right - left, bottom - top) / 2.0
    half *= 1.0 + margin
    side = min(image.width, image.height) / 2.0
    half = min(half, side)
    cx = min(max(cx, half), image.width - half)
    cy = min(max(cy, half), image.height - half)
    return image.crop((int(cx - half), int(cy - half), int(cx + half),
                       int(cy + half)))


def write_ico(name, out_name, sizes):
    """An .ico of plain 32 bit images, which every version of Windows reads."""
    source = trimmed(load(name))
    entries = []
    blobs = []
    offset = 6 + 16 * len(sizes)
    for size in sizes:
        image = source.resize((size, size), Image.LANCZOS)
        pixels = image.load()
        xor = bytearray()
        for y in range(size - 1, -1, -1):  # bottom up, as a BMP is stored
            for x in range(size):
                r, g, b, a = pixels[x, y]
                xor += bytes((b, g, r, a))
        mask_row = (size + 31) // 32 * 4
        mask = bytes(mask_row * size)
        header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0,
                             len(xor) + len(mask), 0, 0, 0, 0)
        blob = header + bytes(xor) + mask
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0,
                                   1, 32, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    path = os.path.join(OUT, out_name)
    with open(path, 'wb') as f:
        f.write(struct.pack('<HHH', 0, 1, len(sizes)))
        for e in entries:
            f.write(e)
        for b in blobs:
            f.write(b)
    print('%-20s %s  <- %s' % (out_name, ', '.join(str(s) for s in sizes), name))


def main():
    if not os.path.isdir(SOURCE):
        raise SystemExit('no source folder: ' + SOURCE)
    os.makedirs(OUT, exist_ok=True)
    write_png(*SIDE)
    write_png(*SMALL)
    write_ico(ICON_SOURCE, 'launcher.ico', ICON_SIZES)
    print('\nBuild the installer again to pick these up:')
    print('  powershell -ExecutionPolicy Bypass -File bwvr/tools/package.ps1')


if __name__ == '__main__':
    sys.exit(main())
