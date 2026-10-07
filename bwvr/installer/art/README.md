# The art

Every file here is made from the source art in `media\images`. Redraw a
source image, then run:

```
python bwvr/tools/make_art.py
powershell -ExecutionPolicy Bypass -File bwvr/tools/package.ps1
```

Nothing here is edited by hand, so a redraw never needs a second pass.

| File | Size | Made from | Where it shows |
| --- | --- | --- | --- |
| `wizard-side.png` | 410 x 785 | `Header-410x785.jpg` | The left side of the first and last pages of setup |
| `wizard-small.png` | 138 x 138 | `Black & White VR Logo - Black.jpg` | The top right of every other page of setup |
| `launcher.ico` | 16, 24, 32, 48, 64, 128 and 256 px | `Black & White VR Logo - Black.jpg` | The launcher, the setup program, the shortcuts, and the Windows list of installed apps |

## What the script does

It changes the size and the format, and nothing else. The one exception is
the icon: the logo is drawn with a wide flat border, and at 16 px that border
is most of the picture, so the script crops to the mark and keeps it square.
See `trimmed` in `bwvr/tools/make_art.py`.

The name in the wordmark is read at a glance down to about 32 px. At 16 px,
which is the size in the taskbar and in a file list, three lines of letters
cannot be read at any quality. A separate small mark, for example the symbol
on its own, would read there. Drop one in `media\images` and the script can
use it for the small sizes.

## Sizes, if the art is redrawn

- **Side image:** 410 x 785, portrait. It shows at about 164 x 314 points,
  and Inno Setup keeps the aspect ratio, so this exact size avoids a crop.
- **Small image:** square. It shows at about 55 points.
- **Icon source:** square, at least 256 x 256. The script makes every size.

Setup follows the light and dark mode of Windows. The logo on black reads as
a dark tile in both.

## Art that the build does not use

`media\images` also holds the wide headers, 1920 x 1080 and 2280 x 760, and
the logo on white. Those are for the video, for a page header and for
anywhere the mod is listed.
