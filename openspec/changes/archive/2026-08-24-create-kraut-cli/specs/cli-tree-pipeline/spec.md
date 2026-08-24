# Spec: cli-tree-pipeline

## ADDED Requirements

### Requirement: Load native .tree descriptors

The CLI SHALL load `.tree` descriptor files produced by the official Kraut editor using the original `KrautGenerator` deserialization code, without modification of the file or the library.

#### Scenario: Load editor-produced descriptor

- **WHEN** the user runs `kraut-cli info <file>.tree` on a file saved by the official Kraut editor
- **THEN** the descriptor loads successfully and a structural summary (spawn node count, branch structure parameters, LOD configuration) is printed

#### Scenario: Reject corrupt or non-descriptor file

- **WHEN** the user runs any `kraut-cli` command against a file that is not a valid `.tree` descriptor
- **THEN** the command exits with a non-zero exit code and prints an error message to stderr

### Requirement: Round-trip descriptor compatibility

The CLI SHALL be able to save a loaded descriptor back to `.tree` format such that the official Kraut editor can open the saved file without errors.

#### Scenario: Load-save-load round trip

- **WHEN** a `.tree` file is loaded by the CLI and immediately saved to a new path
- **THEN** the official Kraut editor opens the saved file and the descriptor is structurally equivalent to the original

### Requirement: Deterministic tree generation

The CLI SHALL generate tree structure, LODs, and meshes via `TreeStructureGenerator`, `TreeStructureLodGenerator`, and `TreeMeshGenerator`, and generation SHALL be deterministic for a given descriptor and seed.

#### Scenario: Same seed, same mesh

- **WHEN** the user runs `kraut-cli generate <file>.tree --seed 42 --out a.obj` twice
- **THEN** both output files are identical

#### Scenario: Different seeds differ

- **WHEN** the user runs generation with `--seed 1` and `--seed 2` on the same descriptor
- **THEN** the two outputs differ

### Requirement: OBJ export

The CLI SHALL export generated meshes in Wavefront OBJ format for a selectable LOD level.

#### Scenario: Export specific LOD

- **WHEN** the user runs `kraut-cli generate <file>.tree --seed 7 --lod 0 --out tree.obj`
- **THEN** a valid OBJ file for LOD 0 is written containing vertex positions, texture coordinates, and faces

### Requirement: Native .kraut export

The CLI SHALL export generated meshes with LOD data in the native `.kraut` baked format readable by Kraut's own importer. If the `.kraut` writer cannot be separated from editor-only code, this requirement MAY be deferred to a follow-up change with OBJ as the interim format.

#### Scenario: Export .kraut file

- **WHEN** the user runs `kraut-cli export <file>.tree --seed 7 --format kraut --out tree.kraut`
- **THEN** a `.kraut` file is written that the reference importer (`Code/KrautViewer/KrautImport.cpp`) parses successfully

### Requirement: Machine-readable output mode

Every `kraut-cli` subcommand SHALL support a `--json` flag producing machine-readable stdout for agent consumption.

#### Scenario: JSON info output

- **WHEN** the user runs `kraut-cli info <file>.tree --json`
- **THEN** stdout contains a single valid JSON document with the descriptor summary and no other prose
