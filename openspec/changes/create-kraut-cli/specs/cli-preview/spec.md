# Spec: cli-preview

## ADDED Requirements

### Requirement: Interactive preview window

The preview binary SHALL open an SDL2 + Dear ImGui + OpenGL window rendering the generated tree mesh with an orbit camera, LOD selector, and wireframe toggle.

#### Scenario: Launch interactive preview

- **WHEN** the user runs `kraut-preview <file>.tree --seed 42`
- **THEN** a window opens showing the generated tree, and the user can orbit the camera, switch LOD levels, and toggle wireframe via the ImGui panel

### Requirement: One-shot screenshot mode

The preview binary SHALL support a `--screenshot <path>` mode that renders the tree once, writes a PNG image, and exits with code 0, without requiring user interaction.

#### Scenario: Agent captures screenshot

- **WHEN** an agent runs `kraut-preview <file>.tree --seed 42 --screenshot out.png`
- **THEN** the process exits with code 0 and `out.png` contains a rendered image of the generated tree

#### Scenario: Screenshot reflects seed and LOD

- **WHEN** screenshots are taken with different `--seed` or `--lod` values
- **THEN** the resulting images visibly correspond to the generated geometry for those parameters

### Requirement: Preview is an optional build component

The preview binary SHALL be gated behind a CMake option so that the headless `kraut-cli` build requires no SDL2, ImGui, or OpenGL dependencies.

#### Scenario: Headless build without preview deps

- **WHEN** the project is configured with `-DKRAUT_CLI_PREVIEW=OFF` on a machine without SDL2/ImGui
- **THEN** `kraut-cli` configures, compiles, and links successfully
