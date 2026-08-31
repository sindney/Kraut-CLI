# add-glb-export — tasks

## 1. glb export

- [x] 1.1 `GlbExport.h/.cpp`: glb writer (header + JSON + BIN chunks), per-LOD material-grouped meshes named `<tree>_LOD<n>`, billboard quad `<tree>_Billboard`, COLOR_0 wind weights, PBR materials, `asset.extras.kraut`
- [x] 1.2 `KrautCLI export --format glb` wiring + JSON summary (textures to copy, warnings)
- [x] 1.3 `KrautPreview --atlas` mode: N-azimuth orthographic billboard atlas with transparent background
- [ ] 1.4 Import one glb in fury3d to validate the contract end-to-end (fury3d `add-kraut-vegetation` section 4)
- [x] 1.5 Determinism check: same descriptor + seed twice -> byte-identical glb
- [x] 1.6 Document the atlas layout in Tools/KrautCLI/README.md

## 2. macOS build support (unblocked by this change)

- [x] 2.1 Darwin platform detection (`CURRENT_OSX_VERSION` was never defined), X11 made Linux-only, per-module include dirs replaced by textual `KrautFoundation/...` include rewrites, MSVC-isms (`__declspec`, `__int64`, `_stricmp`, goto-over-init, temporary-to-nonconst-ref) fixed, platform-conditional GL linking for KrautPreview
