# Doxygen Awesome (vendored)

This directory contains the **Doxygen Awesome** theme, vendored so the
documentation build is reproducible and works offline.

| | |
| --- | --- |
| **Upstream** | <https://github.com/jothepro/doxygen-awesome-css> |
| **Version** | v2.4.2 |
| **Licence** | MIT (see [`LICENSE`](LICENSE)) |
| **Fetched** | 2026-08-09 |

## What is here

- `doxygen-awesome.css` — the theme stylesheet.
- `doxygen-awesome-darkmode-toggle.js` — light/dark mode toggle button.
- `doxygen-awesome-fragment-copy-button.js` — copy button for code fragments.
- `doxygen-awesome-paragraph-link.js` — copyable paragraph anchors.
- `doxygen-awesome-interactive-toc.js` — interactive table of contents.
- `doxygen-awesome-tabs.js` — tabbed content support.
- `doxygen-awesome-sidebar-only*.css` — optional sidebar-only layout variant
  (unused by the current `Doxyfile`, kept for reference).
- `header.html` — Doxygen HTML header adapted for RPG-OS (metadata, the GitHub
  corner, the theme scripts). Based on the upstream example header.
- `custom.css` — small RPG-OS-specific tweaks on top of the theme.
- `LICENSE` — the upstream MIT licence.

## Updating

To upgrade, download the assets from the upstream release tag into this
directory, re-apply the RPG-OS adjustments to `header.html` / `custom.css`
(the upstream files are examples and cannot be copied verbatim), and bump the
version above. Keep the licence file in sync.
