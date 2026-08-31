# glb-export

## ADDED Requirements

### Requirement: `KrautCLI export --format glb` SHALL write a valid glb with the full LOD chain

The export SHALL be a single binary glTF (glb) containing: one mesh node per full-mesh LOD slot named `<tree>_LOD<n>` (LOD 0 = full detail, primitives grouped by material), a billboard quad node `<tree>_Billboard` sized to the tree bounding box, `COLOR_0` vertex colors, PBR materials with external texture URIs, and `asset.extras.kraut` with `seed`, `descriptor`, `billboard` (atlas grid + facing mode + texture), and `lod_thresholds` when the descriptor's LOD distances are non-zero.

#### Scenario: Export contains LOD chain + billboard + extras

- **WHEN** a descriptor with 3 full-mesh LOD slots is exported to glb
- **THEN** the glb contains meshes `<tree>_LOD0..2` and `<tree>_Billboard`
- **AND** every mesh primitive has POSITION, NORMAL, TEXCOORD_0, COLOR_0 and uint32 indices
- **AND** `asset.extras.kraut.billboard.atlas_cols` is present

### Requirement: Wind weights SHALL be deterministic functions of the branch structure

COLOR_0 SHALL encode: R branch sway weight (0 trunk base .. 1 twig tips), G leaf flutter (1 leaf, 0.5 frond, 0 branch), B per-branch phase hash, A `m_uiColorVariation / 255`.

#### Scenario: Trunk base is rigid, twig tips sway

- **WHEN** a vertex belongs to the first node of a Trunk_1 branch
- **THEN** its R weight is <= 0.02
- **AND** a vertex at the last node of a Twigs branch has R >= 0.5

### Requirement: The billboard atlas SHALL follow the locked layout

`KrautPreview --atlas` SHALL render `atlas_cols` orthographic views in one row, cell k from azimuth `((k + 0.5) / cols - 0.5) * 2*pi` around +Z, transparent background, tree fully framed.

#### Scenario: Atlas has the right shape

- **WHEN** `KrautPreview tree.tree --atlas atlas.png --atlas-cols 8 --atlas-res 256` runs
- **THEN** atlas.png is 2048x256 RGBA, exit code 0, and the first/last cells contain non-transparent pixels

### Requirement: glb export SHALL be deterministic

#### Scenario: Byte-identical re-export

- **WHEN** `KrautCLI export --format glb` runs twice with the same descriptor and seed
- **THEN** the two glb files are byte-identical

### Requirement: Texture URIs SHALL resolve to copyable files

`.dds` texture references SHALL resolve to sibling `.tga` files when present (tga/ subdirectory or same-dir extension swap); the export's JSON summary SHALL list every referenced texture's resolved source path and its glb URI basename.

#### Scenario: PalmTree.dds resolves to PalmTree.tga

- **WHEN** a descriptor references `Textures/Bark/Other/PalmTree.dds` and `Textures/Bark/Other/tga/PalmTree.tga` exists
- **THEN** the material's image URI is `PalmTree.tga` and the summary maps it to the resolved source path
