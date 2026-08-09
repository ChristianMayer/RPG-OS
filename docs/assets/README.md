# Branding assets (logo, banner, favicon)

This directory holds the RPG-OS branding assets used by the Doxygen
documentation. Everything here is committed and referenced from the root
`Doxyfile`, `docs/doxygen-awesome/header.html`, and `README.md` (the docs
front page).

| File | Purpose | Format |
| --- | --- | --- |
| `mundus-mirabilis_512.png` | Project logo in the docs header (top-left) | square PNG, 512×512, transparent background |
| `mundus-mirabilis-RPG_OS.png` | Hero banner on the docs front page | wide PNG, 2064×512 (≈4:1) |
| `favicon.png` | Browser tab icon | square PNG, 256×256 |

## Conventions

- **Logo** — a square image (512×512 here) with the motif centred and some
  padding. The theme scales it to fit the ~56 px header row automatically
  (`#projectlogo img { max-height: ... }` in `doxygen-awesome.css`), so a
  larger source simply means crisper rendering on HiDPI displays. Use a
  transparent background (`PROJECT_LOGO` in the `Doxyfile`).
- **Banner** — wide (≈4:1, 2064×512 here). It is embedded at the top of
  `README.md` and scales to the page width on both GitHub and the docs
  (styled by the `div.textblock div.image` rule in `custom.css`).
- **Favicon** — a small square version of the logo. Browsers scale it to the
  tab bar; 256×256 covers all sizes. It is copied into the docs output via
  `HTML_EXTRA_FILES` and linked from `docs/doxygen-awesome/header.html`.
- **Raster assets** (PNG/JPG) — place them in this folder too: the `Doxyfile`
  sets `IMAGE_PATH = docs/assets`, so images embedded in the markdown pages
  resolve. Use PNG for transparency, JPG only for photos.

## Social sharing (optional)

For link previews on social media, add an `og:image` meta tag pointing at a
raster banner on the published site, e.g.
`https://mundus-mirabilis.github.io/RPG-OS/main/mundus-mirabilis-RPG_OS.png`.

## How to replace an asset

Drop your new artwork into this folder **keeping the same filename** (or
update the references in the `Doxyfile` / `README.md` / `header.html`), then
rebuild the docs:

```sh
cmake --build build --target rpg_os_docs   # HTML -> ./html (gitignored)
```
