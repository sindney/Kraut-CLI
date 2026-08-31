# KrautCLI

Headless command line interface for Kraut tree/vegetation generation. Links only
`KrautFoundation` + `KrautGenerator` — no Qt, no OpenGL, fully agent-controllable.

Sibling tool: `Tools/KrautPreview` (SDL2 + Dear ImGui + OpenGL preview, part of the
default build; disable with `-DKRAUT_BUILD_PREVIEW=OFF`).

## Building

The default build is light and fast (KrautCLI + KrautPreview, no Qt):

```sh
cmake -S . -B build-cli -G "Visual Studio 17 2022" -A x64
cmake --build build-cli --target KrautCLI --config Release
# binary: Output/Bin/WinVs2017Release64/KrautCLI.exe
```

Qt is not required for the default build. The full Qt-based `aeEditor` (and its
editor-only engine libraries) is opt-in: configure with `-DKRAUT_BUILD_EDITOR=ON`.

## Commands

```
KrautCLI info <file.tree> [--json]
KrautCLI dump <file.tree>
KrautCLI patch <in.tree> <out.tree> [--seed N] [--copy <FromType> <ToType>] --set <BranchType>.<Field>=<Value> [--set ...] [--json]
KrautCLI generate <file.tree> [--seed N] [--lod none|0|1|2|3|4] --out <file.obj> [--json]
KrautCLI export <file.tree> [--seed N] --format obj|kraut --out <path> [--json]
KrautCLI roundtrip <in.tree> <out.tree> [--json]
```

- `dump`: prints every patchable scalar field of every used branch type as
  `Type.Field = Value` (output is directly reusable as `--set` arguments).
- `patch`: clones a descriptor with typed overrides. Branch type names accept
  `Trunk_1..3`, `Main_Branches_1..3`, `Sub_Branches_1..3`, `Twigs_1..3` (underscore optional,
  case-insensitive); the `m_` field prefix may be omitted. Enum fields accept names
  (`Straight/Upwards/Degree22..Degree157/Downwards`, `Off/Relative/Absolute`, `Default/Umbrella`,
  `Upwards/AlongBranch/OrthogonalToBranch`, `Full/Symetric/InverseSymetric`) or integers.
  Geometry toggles: `EnableBranch`/`EnableFrond`/`EnableLeaf`; sub-type gates: `AllowSubType0..2`;
  texture strings: `TextureBranch`/`TextureFrond`/`TextureLeaf`.
  `--copy A B` clones a full branch-type desc (curves + textures included) into another slot —
  the way to activate an unused type (e.g. Sub_Branches_1) with sane defaults. Copies run before sets.
  Curves and textures are inherited from the source descriptor; `--seed` reseeds the output file.

- `--seed N`: overrides the descriptor's random seed (default: seed stored in the file).
- `--lod`: `none`/`full` = full detail; `0`..`4` = LOD levels. Only full-mesh LODs can be
  generated headlessly (impostor modes FourQuads/TwoQuads/Billboard require the editor renderer).
- `--format obj`: writes one OBJ per full-mesh LOD (`<path>.obj`, `<path>_LOD0.obj`, ...).
- `--format kraut`: writes the native baked `.kraut` format (v2), self-validated after writing.
- `--format glb`: writes one binary glTF (see "glb export" below) for engine import
  (fury3d `add-kraut-vegetation`); the JSON summary lists every referenced texture
  (URI basename + resolved source path) so callers can copy them next to the glb.
- `--json`: machine-readable stdout (single JSON document) for agent consumption.
- Exit codes: 0 ok, 1 usage error, 2 load failure, 3 generation failure, 4 export failure.

## glb export

`KrautCLI export <file.tree> --format glb --out <path>` produces a single glb with:

- one mesh node per full-mesh LOD slot named `<tree>_LOD<n>` (LOD 0 = full detail),
  primitives grouped by material (bark / frond / leaf);
- a billboard quad node `<tree>_Billboard`, pre-sized to the tree bounding box
  (width = XZ diagonal so the silhouette fits from every azimuth, height = bbox height),
  referencing `<tree>_BillboardAtlas.png` (rendered separately by `KrautPreview --atlas`);
- `COLOR_0` vertex colors with wind weights: **R** branch sway (0 trunk base .. 1 twig tips),
  **G** leaf flutter (1 leaf, 0.5 frond, 0 branch), **B** per-branch phase hash,
  **A** `m_uiColorVariation / 255`;
- PBR materials: baseColor texture + `MASK` alpha (cutoff 0.4) + `doubleSided` for
  frond/leaf geometry. `.dds` texture references resolve to `.tga`/`.png` siblings
  (a `tga/`/`png/` subdirectory or a same-dir extension swap) because downstream
  STB-based loaders cannot read DDS; unresolved references are reported as warnings;
- `asset.extras.kraut`: `{ seed, descriptor, reference_fov: 0.7854, lod_thresholds?,
  billboard: { atlas_cols, atlas_rows: 1, mode: "cylindrical", texture, quad_width,
  quad_height }, wind: { encoding } }`. `lod_thresholds` are screen-coverage
  fractions converted from the descriptor's LOD distances via the tree's bounding
  radius and the reference fov (omitted when every slot distance is 0; the importer
  synthesizes linear thresholds then).

Determinism: same descriptor + seed -> byte-identical glb.

## Billboard atlas layout (locked)

`KrautPreview <file.tree> --atlas out.png [--atlas-cols N] [--atlas-res R]` bakes the
billboard atlas: `N` orthographic views in a single row (`atlas_rows = 1`; cylindrical
facing), cell size `R x R`, transparent background.

- Cell `k` (0-based) shows the tree from azimuth `a_k = ((k + 0.5) / cols - 0.5) * 2*pi`
  radians around the +Y axis, where azimuth 0 = camera on the tree's **+Z** axis.
- The fury BILLBOARD shader maps camera azimuth `angle = atan2(fwd.x, fwd.z)` to cell
  `floor(fract(angle / 2pi + 0.5) * cols)` — the same `a_k`, so cell centers align.
- The ortho box exactly covers the glb billboard quad: `[-quadW/2, quadW/2] x [0, quadH]`
  (quad width = XZ bbox diagonal, height = bbox height), so the image maps edge-to-edge
  onto the quad's UVs.
- Quad UVs: u 0..1 across the cell, v 1 at the trunk base .. 0 at the top (glTF v-down).


## Data compatibility

- `.tree` load supports **file versions 14–18** (all Kraut Beta 3 SDK files + current repo files).
- `.tree` save always writes **file version 16 with SpawnNodeDesc v42** — the newest format the
  released Kraut Beta 3 binary can open. (Repo HEAD writes v18/v43, which crashes Beta 3:
  v43 removed the normal-map texture strings, desyncing Beta 3's v42 reader. v16 files are
  still readable by newer editors via their version gates.)
- Note: Beta 3 SDK `.tree` files may carry trailing data after the descriptor (editor thumbnail
  etc.) that no reader consumes; the CLI drops it on re-save.
- Known engine-inherent quirk: `Curve::PasteCurve` resamples curves through a division on every
  load, so repeated load→save cycles oscillate a few float samples by ±1 ulp. The editor does
  the same; semantic content is unaffected.
- `.kraut` export caveats: per-vertex AO is written as 1.0 (AO baking needs the editor renderer),
  impostor LODs are skipped, and material diffuse names are the descriptor's material names
  (no material-library resolution).

## Agent workflow example

```sh
# inspect a descriptor
KrautCLI info Data/Content/Trees/Tree5.tree --json

# generate + export OBJ
KrautCLI generate Data/Content/Trees/Tree5.tree --seed 42 --lod none --out tree.obj --json

# visual verification (KrautPreview is part of the default build)
KrautPreview Data/Content/Trees/Tree5.tree --seed 42 --screenshot tree.png

# native baked format
KrautCLI export Data/Content/Trees/Tree5.tree --seed 42 --format kraut --out tree.kraut --json
```

## Official-render screenshots (aeEditor)

The built editor (Qt 5.14.1 at `D:\SDK\QT`) has a headless screenshot mode that renders with the
**official** pipeline (textures, materials, lighting, LODs):

```
aeEditor.exe --screenshot <in.tree> <out.png> [--seed N] [--width W] [--height H]
```

- Prints a one-line JSON result on stdout; exit codes: 0 ok, 1 usage, 2 load failure, 3 render failure.
- Renders into an FBO with the editor's export-preview path (tree only: no ground plane/collision/forces).
- Qt DLLs + `ezKrautEditorRenderAPI_GL.dll` must sit next to the exe (they are copied/built into
  `Output/Bin/WinVs2017Release64/`).

## Agent workflow example

```sh
# inspect a descriptor
KrautCLI info Data/Content/Trees/Tree5.tree --json

# generate + export OBJ
KrautCLI generate Data/Content/Trees/Tree5.tree --seed 42 --lod none --out tree.obj --json

# visual verification with the OFFICIAL renderer (recommended)
Output/Bin/WinVs2017Release64/aeEditor.exe --screenshot Data/Content/Trees/Tree5.tree tree.png --seed 42

# native baked format
KrautCLI export Data/Content/Trees/Tree5.tree --seed 42 --format kraut --out tree.kraut --json
```

## Preview tool (KrautPreview, lightweight alternative)

```
KrautPreview <file.tree> [--seed N] [--lod none|0|1|2|3|4]
KrautPreview <file.tree> [--seed N] [--lod ...] --screenshot out.png [--width W] [--height H]
```

A compact SDL2+ImGui preview with a plain lambert shader (no textures). Interactive: drag to orbit,
wheel to zoom, ImGui panel for seed/LOD/wireframe/regenerate. `--screenshot` renders offscreen to PNG
and exits 0. It is part of the default build (disable with `-DKRAUT_BUILD_PREVIEW=OFF`); the first
configure requires network access once (FetchContent: SDL2, Dear ImGui, GLEW, stb).
For textured/official-quality images, use `aeEditor --screenshot`.
