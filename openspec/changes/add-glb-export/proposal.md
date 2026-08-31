# add-glb-export

## Why

Kraut-CLI exports OBJ (no LODs, no vertex colors, no materials worth speaking of) and `.kraut` v2 (needs a custom reader, no extras channel). The fury3d engine (companion change `add-kraut-vegetation`) needs a single-file bridge format carrying the full LOD chain, billboard quad + atlas reference, wind weights, and PBR materials: glTF-binary with kraut metadata in `asset.extras.kraut`.

## What Changes

- `KrautCLI export --format glb`: one glb per tree containing
  - one mesh node per full-mesh LOD slot named `<tree>_LOD<n>` (LOD 0 = full detail), primitives grouped by material,
  - a billboard quad node `<tree>_Billboard` sized to the tree's bounding box,
  - `COLOR_0` vertex colors carrying wind weights (R=sway, G=flutter, B=phase, A=color variation),
  - PBR materials (baseColor + MASK alpha for leaf/frond, doubleSided) with external texture URIs (basename; .dds resolved to .tga siblings when present),
  - `asset.extras.kraut` = { seed, descriptor, lod_thresholds (+ reference_fov), billboard: { atlas_cols, atlas_rows, mode, texture }, wind encoding }.
- `KrautPreview --atlas out.png [--atlas-cols N] [--atlas-res R]`: renders the tree from N azimuths around +Y (orthographic) into a 1-row billboard atlas PNG with transparent background. Cell k is rendered from azimuth `((k + 0.5) / cols - 0.5) * 2*pi` around the +Z axis (contract with the fury BILLBOARD shader).
- Billboard atlas layout documented in Tools/KrautCLI/README.md.
- Round-trip determinism: same descriptor + seed -> byte-identical glb (verified in tasks).

## Capabilities

### Modified Capabilities

- `cli-tree-pipeline`: the export command gains the glb format; determinism now also covers the glb output.

### New Capabilities

- `glb-export`: the glb writer, wind-weight derivation, billboard quad, extras contract, and the atlas-rendering mode of KrautPreview.
