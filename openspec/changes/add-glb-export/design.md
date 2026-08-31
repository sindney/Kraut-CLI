# add-glb-export — design

## Context

The fury3d engine imports trees through its GltfImporter, which auto-groups `<base>_LOD<n>` mesh names into a LOD chain. glb is the bridge (design D2 in fury3d's add-kraut-vegetation change): one file with LODs, billboard, wind weights, materials, and extras. Kraut does not compute wind weights itself — `Kraut::Vertex::m_uiBranchNodeIdx` exists so games can map branch-structure data to vertices; the exporter derives weights from it.

## Decisions

### D1: glb written directly (no library)

12-byte header + JSON chunk + BIN chunk, hand-assembled. The JSON surface is a fixed schema (no arbitrary escaping needed beyond paths, which are controlled). No new dependencies.

### D2: Wind weights derived from branch structure

Per vertex, from `m_uiBranchNodeIdx` and the owning branch structure:

- **R (sway)**: `clamp01(base[type] * (0.35 + 0.65 * t))` where `t = nodeIdx / (nodeCount - 1)` along the branch and `base` is a per-branch-type flexibility (Trunk 0.02/0.03/0.04, Main 0.10/0.14/0.18, Sub 0.25/0.32/0.40, Twig 0.55/0.65/0.75).
- **G (flutter)**: 1.0 Leaf, 0.5 Frond, 0.0 Branch geometry.
- **B (phase)**: integer hash of the branch-structure index -> [0, 1), so branches sway out of sync deterministically.
- **A (color variation)**: `m_uiColorVariation / 255`.

### D3: Billboard quad is pre-sized; atlas is a separate render step

The quad spans the tree bbox (width = max XZ extent, height = Y extent), so its mesh AABB culls correctly. The atlas is NOT baked by KrautCLI (it has no GL) — `KrautPreview --atlas` renders it with the same descriptor + seed. The glb references `<stem>_BillboardAtlas.png` by relative URI; `extras.kraut.billboard` carries the grid + facing mode. `fury kraut generate` orchestrates both tools (documented in the fury3d change).

### D4: LOD thresholds converted from Kraut distances

Kraut LOD slots carry `m_uiLodDistance` (meters). Fury thresholds are screen-coverage fractions. Conversion uses the tree's full-detail bounding radius and a reference fov of 0.7854 rad (recorded in extras): `threshold_i = clamp01(radius_cm / (dist_i_m * 100) / tan(fov/2))`. The billboard tier's threshold uses `1.5x` the deepest full-mesh slot's distance. When every slot distance is 0, `lod_thresholds` is omitted and the importer synthesizes linear thresholds.

### D5: Texture URIs are basenames; .dds resolved to .tga when possible

glTF image URIs reference the texture basename next to the glb; the export JSON summary lists the resolved source files for the caller to copy. `.dds` references resolve to sibling `.tga` files (`tga/` subdirectory or same-dir extension swap) because fury's STB-based loader cannot read DDS. Unresolvable references keep the original basename and are reported as warnings.

## Atlas layout (locked)

- Grid: `atlas_cols` cells in a single row (`atlas_rows = 1` in v1; cylindrical facing).
- Cell k (0-based) is the tree seen from azimuth `a_k = ((k + 0.5) / cols - 0.5) * 2*pi` radians around the +Y axis, where azimuth 0 means the camera sits on the tree's +Z axis looking at the trunk. The fury BILLBOARD shader computes the same `a_k` from the camera position and picks `floor(fract(angle / 2pi + 0.5) * cols)`.
- Each cell is an orthographic view centered on the tree bbox, square aspect, transparent background (alpha 0), tree fully framed with a 5% margin. Lighting matches KrautPreview's preview shading.
