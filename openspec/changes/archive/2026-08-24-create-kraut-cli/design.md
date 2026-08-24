# Design: create-kraut-cli

## Context

Kraut is a procedural tree/vegetation generator by Jan Krassnigg, built on a self-contained "AE" mini-engine (`KrautFoundation` / `KrautGenerator` / `KrautGraphics`) with a Qt5 editor (`Tools/aeEditor`). It is **not** ezEngine-based (the `FindezEngine.cmake` is unused scaffolding).

Key facts established by codebase exploration:

- `KrautGenerator` links **only** `KrautFoundation` and has zero rendering/Qt dependencies. The full pipeline is pure CPU:
  - `Description/TreeStructureDesc.h` — descriptor with `Serialize`/`Deserialize` (binary `.tree` format via `Kraut::Serializer` over `aeStreamIn/aeStreamOut`, impl in `Serialization/SerializeTree.cpp`)
  - `TreeStructure/TreeStructureGenerator.h` — `GenerateTreeStructure(seed)` → `TreeStructure`/`BranchStructure`
  - `Lod/TreeStructureLodGenerator.h` — LOD generation
  - `Mesh/TreeMeshGenerator.h` — `GenerateTreeMesh()` → plain vertex/index buffers
  - Usage pattern: set public member pointers (`m_pTreeStructureDesc`, `m_pTreeStructure`, …), then call `Generate*()`.
- `.kraut` baked format writer: `TreePlugin/Mesh/Export/TreeExport.cpp`; reader reference: `Code/KrautViewer/KrautImport.cpp`.
- OBJ exporter: `TreePlugin/Mesh/Export/ExportOBJ.cpp` — depends only on mesh structs, trivially portable.
- The custom CMake build (`Code/BuildSystem/CMake/`) **auto-globs `Code/Engine/*/CMakeLists.txt` and `Code/Tools/*/CMakeLists.txt`** (`CMAKE_GlobProjects.txt:18-21`), so a new tool folder is integrated by convention, not by editing build files.
- Qt support is **opt-in per project** (`CMAKE_Qt.txt:3` gates on `EZ_ENABLE_QT_SUPPORT AND EZ_QTPROJECT`), so a CLI tool target configures and builds with no Qt.
- Libraries are built with an MSVC precompiled-header convention (`ADD_MSVC_PRECOMPILED_HEADER`, `PCH.h` per project); a new target must follow the same convention (or provide its own `PCH.h`).

Decision change during apply: the user chose to build the CLI **in this repo** (not a separate `D:\git\Kraut-cli` repo), and to leave changes uncommitted for manual verification.

## Goals / Non-Goals

**Goals:**

- New tool target `Tools/KrautCLI/` producing `KrautCLI.exe` with subcommands `info`, `generate`, `export` (OBJ + native `.kraut`), `--seed`, `--lod`, `--out`, `--json`.
- Optional target `Tools/KrautPreview/` (CMake option `KRAUT_BUILD_PREVIEW`, default OFF): SDL2 + Dear ImGui + OpenGL window with orbit camera, LOD selector, wireframe toggle, and one-shot `--screenshot <path>` mode for agent verification.
- Bit-exact `.tree` compatibility by linking the actual `KrautGenerator` serializer — load editor files, save CLI files, open them in the GUI.
- Deterministic generation for a given (descriptor, seed) pair so agents can diff outputs.
- Compact: a handful of small `.cpp` files; headless target has zero third-party dependencies.

**Non-Goals:**

- No modification of existing engine/editor source files.
- No descriptor *editing* DSL in v1 (agents get load/generate/export/save; parameter editing stays in the GUI).
- No FBX export, no physics, no impostor/atlas baking (editor-only features requiring `KrautGraphics`).
- No ezEngine integration; the design does not preclude a later migration.
- No network/server mode — plain CLI exit-code semantics.
- No commits/pushes — the user verifies changes manually first.

## Decisions

### D1: In-repo tool target instead of a separate repository

Add `Tools/KrautCLI/` to the existing glob build.

- *Why*: (a) the build system auto-discovers `Code/Tools/*` — integration is a 5-line CMakeLists; (b) same source = guaranteed format compatibility, no cross-repo sync problem; (c) PCH/define conventions (`KRAUT_DLL`, `AE_FOUNDATION_DLL`, include dirs) are already solved here; (d) user explicitly preferred this after investigation.
- *Alternative rejected*: separate `D:\git\Kraut-cli` repo with `KRAUT_ROOT` path reference — re-solves build plumbing for zero benefit and adds a sync hazard.

### D2: Link KrautGenerator/KrautFoundation as-is; port only the tiny exporters

The CLI links the engine libs directly. The OBJ writer (and, if separable, the `.kraut` writer) is *copied* into `Tools/KrautCLI/` with editor dependencies stripped, because `TreePlugin` pulls in Qt/OpenGL and cannot be linked.

- *Why*: keeps engine code untouched (zero regression risk to the editor) while reusing the one piece that matters bit-for-bit (the serializer).
- *Alternative rejected*: link `TreePlugin` — drags Qt5, OpenGL, Bullet, FBX SDK into a headless tool.

### D3: `.kraut` and OBJ as the only export formats in v1

- *Why*: `.kraut` gives compatibility with Kraut's own viewer; OBJ gives universal agent/tooling consumption. Both writers depend only on mesh structs.
- *Risk*: if `TreeExport.cpp` proves too entangled with editor types (impostors, texture atlases), v1 ships OBJ-only and `.kraut` moves to a follow-up change (tracked in Open Questions with a fallback).

### D4: SDL2 + Dear ImGui + OpenGL 3.3 for the preview, separate target, OFF by default

- *Why*: user-requested stack; imgui gives an instant control panel (seed, LOD selector, wireframe, reload-on-file-change); a `--screenshot` one-shot mode renders offscreen, writes PNG, and exits — the agent's "eyes". Separate target + CMake option keeps the headless build dependency-free.
- *Alternatives*: (a) reuse `KrautGraphics` — drags in Lua, resource manager, legacy GL, rejected (violates compactness); (b) raylib/sokol — fine but user named SDL+imgui explicitly.

### D5: Deterministic, script-friendly CLI contract

Subcommands with `--seed`, `--lod`, `--out`, `--format` flags; human-readable stdout summary plus `--json` machine-readable mode; non-zero exit codes with stderr messages on failure. Natively agent-controllable without an MCP layer.

## Risks / Trade-offs

- [PCH/build-system conventions may fight a new target (required `PCH.h`, `ADD_MSVC_PRECOMPILED_HEADER`, per-project defines)] → Mirror an existing simple project (`KrautGenerator`) exactly: own `PCH.h`, same include style. Verify by building only the new target.
- [`.kraut` writer entangled with editor types (impostors, textures)] → Fallback: v1 = OBJ only; `.kraut` write support becomes a follow-up change. `.tree` compatibility (the critical one) is unaffected since it lives entirely in `KrautGenerator`.
- [Binary `.tree` format may have version quirks] → First milestone includes a load→save→load round-trip check against every `Data/Content/Trees/*.tree` sample, verified structurally (and by opening a CLI-saved file in the GUI).
- [Glob build adds the new folder to *every* configure, including the user's editor builds] → The new target is additive only; if it failed it would break configure for everyone, so milestone 1 is "configure + build succeeds" before any CLI logic is written.
- Trade-off accepted: preview duplicates a tiny renderer (~300 lines of GL) instead of reusing `KrautGraphics` — the price of compactness.

## Migration Plan

N/A — additive change, no behavior modified. Rollback = delete `Tools/KrautCLI/` and `Tools/KrautPreview/`. Nothing is committed until the user verifies.

## Open Questions

1. Does `TreeExport.cpp` separate cleanly from editor/impostor code? (Resolved by the milestone-3 spike; fallback per D3.)
2. Should the CLI support descriptor patching (`--set branch.count=5`) later? Deferred until round-trip fidelity is proven.
3. Exact end-to-end verification tree: user asked for a Populus (poplar) generation run as the acceptance test once the CLI works.
