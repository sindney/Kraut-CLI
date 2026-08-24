# Kraut

Kraut is a program to procedurally generate plants, especially trees. Through a set of parameters you specify how a tree is supposed to grow. Various different, but similar trees can be generated from the same specification. Additionally there is a manual editing mode, which allows to easily paint branches by hand, such that very unique trees can be created.

## General Feature Set

* Procedural growth of branches and fronds.
* Easily create thousands of variations from the same tree specification.
* Interactive WYSIWYG editor.
* Undo / Redo system.
* Export Meshes to OBJ, FBX or custom format.
* "Leafcard" mode to easily create textures which can be used to represent entire branches. This enables you to create highly detailed looking trees without many polygons.
* Exported leafcard textures are automatically processed to be well suited for mipmap creation.
* Automatic computation of up to five level-of-detail meshes with much reduced polygon counts.
* LODs include Impostor and Billboard mode.
* Many tweakable options to get the best quality out of level-of-detail meshes.
* Manual editing mode that allows to easily paint unique branches exactly as desired.
* On manually created branches additional procedural branches may be grown.
* "Physical Simulation" mode: Branches will try to grow around obstacles, such as other branches or additional objects that can be loaded from OBJ files. This allows to create a tree that shall fit perfectly into some corner of a game level.
* Several sample trees and sample leafcards included.

## CLI & Headless Tooling

This fork adds an agent-controllable, headless toolchain so trees can be generated, exported and screenshotted without opening the GUI — e.g. by scripts or AI agents. All tools are built in-repo from `Tools/` (glob-discovered by the existing CMake setup) and land next to the editor in `Output/Bin/<config>/`.

### Components

| Tool | Path | Purpose |
|---|---|---|
| **KrautCLI** | `Tools/KrautCLI` | Headless pipeline: `info` / `generate` / `export` / `roundtrip` on `.tree` files. Deterministic per `(descriptor, seed)`, JSON output for automation, exit codes 1 usage / 2 load / 3 generate / 4 export. Exports OBJ and `.kraut` v2. |
| **KrautPreview** | `Tools/KrautPreview` | Lightweight SDL2 + ImGui + GL3.3 viewer (part of the default build; disable with `KRAUT_BUILD_PREVIEW=OFF`). Orbit camera, LOD selector, wireframe, `--screenshot out.png` batch mode. Textured: diffuse textures (incl. DDS DXT1/DXT5), billboard-leaf expansion and alpha testing mirror the official renderer; normal maps / AO / color variation are editor-only. `--data DIR` adds texture search roots (the repo's `Data/` is probed automatically). |
| **aeEditor --screenshot** | `Tools/aeEditor` | `aeEditor.exe --screenshot in.tree out.png [--seed N] [--width W] [--height H]` — renders with the **official** OpenGL renderer (textured, lit) via a hidden window + offscreen FBO, no GUI shown. This is the way to get presentation-quality images headlessly. |

### Build options

The default build is light and fast: `KrautCLI` + `KrautPreview` (+ `KrautFoundation`/`KrautGenerator`), no Qt required. The full Qt-based editor and its editor-only engine libraries (`KrautGraphics`, `KrautEditorBasics`, `KrautEditorRenderAPI_GL`, `TreePlugin`) are built on demand with `-DKRAUT_BUILD_EDITOR=ON` (requires Qt5).

### Why it was built this way

* **In-repo, not a fork.** The CLI links the same `KrautFoundation` / `KrautGenerator` libraries as the editor, so generation results are bit-identical with the GUI and the code can never drift apart. Only the file-stream and physics hooks are replaced (`FileStreams.h`, `Physics_EmptyImpl`) to cut the Qt/Bullet dependencies.
* **`.tree` writer targets format v16 / SpawnNodeDesc v42, not the repo's native v18/v43.** The released **Kraut Beta 3** binary crashes (stream desync) on v18 files because v43 removed three normal-map strings that its v42 reader still expects. Writing v16 keeps every released editor able to open CLI-produced files; the reader accepts v14–v18 so the CLI handles both old SDK assets and current ones.
* **Round-trip is value-exact, not byte-exact.** `Curve::PasteCurve` resamples control points on every load (±1 ulp float oscillation) — the original editor behaves the same; it is engine-inherent, not a CLI bug.
* **Two renderers on purpose.** KrautPreview exists because it's tiny and dependency-light (shape debugging in CI). The aeEditor screenshot mode exists because the official renderer's export-preview path (`bForExportPreview=true`) gives the real textured/lit look. The screenshot mode does a minimal manual render (own FBO, camera from bounding box, `g_Tree.Render`) instead of the editor's full frame pipeline, because that pipeline dereferences GUI widget singletons that don't exist without the Qt main window; a few null-guards in `TreePlugin`/`aeEditor` make the engine code headless-safe without changing GUI behavior.
* **`AE_TREEGEN_DLL` decorations** on `aeTree`, `aeTreePlugin` and `aeGlobals` let the editor executable link TreePlugin symbols directly; harmless for the DLL build.

See `Tools/KrautCLI/README.md` for full usage of the CLI.

## License

Tree-meshes generated with Kraut Tree Creator can be used free of charge for both commercial and non-commercial projects. However, it would be nice if you let us now about it and also to mention Kraut in your application's credits.

## Screenshots

![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/FrondColorVariation.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/KrautTreeCreator2.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/KrautTreeCreator3.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/KrautTreeCreator4.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/LeafCardCreation.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/PalmTree1.jpg?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/PalmTree2.jpg?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/Cactus1.jpg?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/Grass.png?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/Tree1.jpg?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/Tree2.jpg?raw=true)
![img](https://github.com/jankrassnigg/Kraut/blob/main/Screenshots/Tree3.jpg?raw=true)

