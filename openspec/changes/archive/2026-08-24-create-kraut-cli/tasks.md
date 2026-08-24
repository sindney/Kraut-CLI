# Tasks: create-kraut-cli

> Approach updated during apply: in-repo tool targets (not a separate repo). No commits — user verifies first.

## 1. Build scaffold

- [x] 1.1 Create `Tools/KrautCLI/` with `CMakeLists.txt` following the repo convention (`LINK_LIBRARIES KrautFoundation KrautGenerator` + `CMAKE_Application.txt`), plus a minimal `PCH.h`/`PCH.cpp` matching the repo's PCH pattern
- [x] 1.2 Add a stub `main.cpp` that prints usage; configure CMake and build **only** the `KrautCLI` target with MSVC to prove the scaffold compiles and links
- [x] 1.3 Confirm the full configure still succeeds (glob build picks up the new folder without breaking existing targets)

## 2. Core pipeline

- [x] 2.1 Implement descriptor load: `LoadDescriptor(path)` via `aeFileIn` + `Kraut::Deserializer` (5 LODs + TreeStructureDesc), returning an owning struct
- [x] 2.2 Implement `GenerateTree(desc, lods, seed, lodIndex)` wrapping `TreeStructureGenerator` → `TreeStructureLodGenerator` → `TreeMeshGenerator` (member-pointer pattern per the generator headers)
- [x] 2.3 Implement `SaveDescriptor(desc, lods, path)` via `Kraut::Serializer` + `aeFileOut` for `.tree` round-trip support

## 3. Export

- [x] 3.1 Port OBJ writer from `TreePlugin/Mesh/Export/ExportOBJ.cpp` into `KrautCLI` (mesh structs only, no editor deps)
- [x] 3.2 Spike: assess `.kraut` writer portability from `TreePlugin/Mesh/Export/TreeExport.cpp`; if separable, port it; if not, document deferral and gate `--format kraut` behind an explicit error
- [x] 3.3 Validate exported `.kraut` against the reference reader `Code/KrautViewer/KrautImport.cpp` (if 3.2 succeeded) — **finding:** the KrautViewer reader is outdated (v1 only); editor writes v2 (adds per-vertex AO). Implemented `ValidateKrautFile` (v2 structural reader) instead; export is self-validated on every run.

## 4. CLI interface

- [x] 4.1 Implement subcommands `info`, `generate`, `export` with flags `--seed`, `--lod`, `--out`, `--format obj|kraut`, `--json`
- [x] 4.2 Non-zero exit codes + stderr error messages on load/generate/export failure
- [x] 4.3 `--json` mode: single valid JSON document on stdout for every subcommand

## 5. Compatibility & determinism checks

- [x] 5.1 Round-trip: load → save → load every `Data/Content/Trees/*.tree` sample; assert structural equivalence (compare re-serialized bytes) — all 20 samples pass; note: ±1 ulp float oscillation in curve samples is engine-inherent (`Curve::PasteCurve` resamples through division on every load, same in the editor)
- [x] 5.2 Determinism: same descriptor + seed twice → byte-identical OBJ output
- [x] 5.3 Report a CLI-saved `.tree` to the user for manual open-check in the official GUI (agent cannot run the GUI itself) — **v1 attempt FAILED in user's Kraut Beta 3 binary (crash)**. Root cause found: Beta 3 writes/reads **file version 16 with SpawnNodeDesc v42**; the repo HEAD writes v18/v43 (v43 dropped normal-map strings → v42 reader desyncs → crash). Fixed: CLI now reads v14–v18 and always **writes v16 + SpawnNodeDesc v42** (validates to exact EOF with an independent Python parser). All 20 SDK files + 20 repo files load; v16 output regenerates the identical mesh. **Verified by user: Beta 3 opens the CLI-saved file fine.**

## 6. Preview target (optional, `KRAUT_BUILD_PREVIEW=ON`)

- [x] 6.1 Add `Tools/KrautPreview/` with SDL2 + Dear ImGui + GL 3.3 mesh renderer (vendored or FetchContent, kept out of the default build)
- [x] 6.2 ImGui panel: seed input, LOD selector, wireframe toggle, regenerate button, orbit camera
- [x] 6.3 `--screenshot <path>` one-shot mode: render offscreen, write PNG, exit 0
- [x] 6.4 Verify default build (`KRAUT_BUILD_PREVIEW=OFF`) needs no SDL/GL deps

## 7. End-to-end verification (via subagent)

- [x] 7.1 Spawn a subagent to use the built CLI to generate a **Populus** (poplar) tree: load an appropriate sample descriptor (or the closest in `Data/Content/Trees/`), generate with a fixed seed, export OBJ, and report mesh stats
- [x] 7.2 Subagent validates output (OBJ parses, triangle/vertex counts plausible, determinism re-run matches) and reports results

## 8. Official-render screenshot mode (aeEditor)

> Added after the v16 fix: user provided Qt 5.14.1 at D:\SDK\QT, making the official editor buildable. Goal: official-quality screenshots (textures/materials/AO) so agents verify with the real renderer while KrautCLI stays generation-focused.

- [x] 8.1 Build aeEditor with Qt 5.14.1 (msvc2017_64) — configure + build pass
- [x] 8.2 Add `--screenshot <in.tree> <out.png> [--seed N] [--width W] [--height H]` headless mode to `Tools/aeEditor/MainEd.cpp` (hidden Win32 window + existing RenderAPI_GL context, drives one frame through the normal event pipeline, reads back PNG via QImage)
- [x] 8.3 Export plugin classes for linking (`AE_TREEGEN_DLL` on aeTree/g_Tree, aeTreePlugin/g_Plugin, aeGlobals/g_Globals; link TreePlugin + opengl32 into aeEditor)
- [x] 8.4 Verify screenshot output visually (real textures/materials) and re-run Populus verification through the official renderer — **works**: `aeEditor.exe --screenshot` produces textured/lit trees (verified visually, 5791 tris matches LOD0 OBJ export). Notes: required building KrautEditorRenderAPI_GL target (runtime-loaded plugin), null-guards in exe MessageHandler/qtMainWindow + TreePlugin Render.cpp UpdateStats (GUI pointers absent in headless), skipping the aeEditor_BeforeFirstFrame broadcast, rendering into an FBO (hidden 64x64 window framebuffer is undersized), and resolving FBO GL procs via wglGetProcAddress with __stdcall (engine's glew symbols are private to KrautGraphics.dll). Render skips g_Plugin.Render() entirely — minimal manual render + `g_Tree.Render(..., bForExportPreview=true)`.

## 9. Wrap-up

- [x] 9.1 Short usage doc (header comment in main.cpp or README in KrautCLI folder): subcommands, flags, `--json` contract, screenshot workflow
- [x] 9.2 Report all changes to the user for verification — **do not commit** (superseded: user verified and directed commit to the `sindney/Kraut-CLI` fork)
