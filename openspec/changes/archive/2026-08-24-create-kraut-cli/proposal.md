# Proposal: create-kraut-cli

## Why

Kraut's tree generation is only reachable through the Qt GUI editor (`aeEditor`), which makes it unusable for AI agents and automation: no scripting surface, no batch processing, no way to iterate on a tree without a human clicking in the editor. The generation core (`KrautGenerator` + `KrautFoundation`) is already fully headless (pure CPU, zero rendering/Qt dependencies), so a compact CLI can expose the complete pipeline — load `.tree` descriptor → generate structure → LODs → mesh → export — without dragging in Qt, OpenGL, or the editor stack.

## What Changes

- Add a **new tool target `Tools/KrautCLI/`** inside this repo (auto-picked-up by the glob build at `Code/BuildSystem/CMake/CMAKE_GlobProjects.txt:20`), linking **only `KrautFoundation` + `KrautGenerator`** — no Qt, no OpenGL, no editor plugins. Qt is opt-in per target (`CMAKE_Qt.txt` requires `EZ_QTPROJECT`), so the CLI builds dependency-free.
- Provide headless subcommands:
  - `info` — load a `.tree` descriptor and print a structural summary (branch counts, spawn params, LOD config), with `--json` machine-readable mode.
  - `generate` — run the full pipeline (`TreeStructureGenerator` → `TreeStructureLodGenerator` → `TreeMeshGenerator`) with a given seed and export a mesh.
  - `export` — write generated meshes to **OBJ** and to the native **`.kraut`** baked format, so output can be loaded by Kraut's own viewer/importers.
- **Data compatibility is a hard requirement**: `.tree` files written by the official editor load in the CLI unchanged, and descriptors saved by the CLI round-trip back into the GUI editor (same serializer code, same repo).
- Optional **preview target `Tools/KrautPreview/`**: a lightweight **SDL2 + Dear ImGui + OpenGL** window rendering the generated mesh, with a one-shot `--screenshot` mode so an agent can visually verify results, and interactive orbit camera for humans. Gated behind a CMake option so the headless build stays dependency-free. The official editor remains the tool of record for full editing.
- Note: an earlier assumption that this codebase is ezEngine-based was wrong — it is the standalone "AE" mini-engine (`FindezEngine.cmake` is unused scaffolding). The CLI design is engine-agnostic and does not block a future ezEngine migration.

## Capabilities

### New Capabilities

- `cli-tree-pipeline`: Headless load/generate/LOD/mesh/export pipeline over `.tree` descriptors, with native `.tree`/`.kraut` format compatibility and OBJ export.
- `cli-preview`: Optional SDL2 + Dear ImGui + OpenGL preview window for visual verification of generated trees, including a screenshot capability for agent inspection.

### Modified Capabilities

(none — no existing behavior changes; only new tool targets are added)

## Impact

- **New code**:
  - `Tools/KrautCLI/` — headless CLI (CMakeLists + a handful of `.cpp` files)
  - `Tools/KrautPreview/` — optional SDL2/imgui preview (only built when enabled)
- **Reused code** (unchanged, linked as libraries):
  - `Code/Engine/KrautFoundation` (math, streams, strings)
  - `Code/Engine/KrautGenerator` (descriptors, `TreeStructureGenerator`, `TreeStructureLodGenerator`, `TreeMeshGenerator`, binary `.tree` serializer in `Serialization/SerializeTree.cpp`)
- **Ported code** (small files copied into KrautCLI, editor deps stripped):
  - OBJ export logic from `TreePlugin/Mesh/Export/ExportOBJ.cpp`
  - `.kraut` writer format from `TreePlugin/Mesh/Export/TreeExport.cpp` (reader reference: `Code/KrautViewer/KrautImport.cpp`)
- **No changes** to existing engine/editor code; the GUI editor remains fully functional.
- **New dependencies** (preview target only): SDL2, Dear ImGui, system OpenGL.
