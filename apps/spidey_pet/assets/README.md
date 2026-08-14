# Spidey Pet assets

Drop source images here. They are converted into embeddable drawing headers
with:

```bash
python tools/img2cheeko.py apps/spidey_pet/assets/<name>.png apps/spidey_pet/src/<name>_img.h --name <name> --width 200
```

The generated `src/*_img.h` headers are what the app includes — the images
themselves are never shipped to the device directly.

| File | Used for | Notes |
| --- | --- | --- |
| `mask.png` | Idle screen + focused-mode emblem | Square-ish logo works best; background is auto-detected from the corners and skipped |

Keep sources reasonably small (300–800px). Flat-color art converts far better
than photos: fewer colors = fewer runs = faster draws on the device.
