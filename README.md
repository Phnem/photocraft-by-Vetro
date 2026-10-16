<h1 align="center">PhotoCraft by Vetro</h1>

<p align="center">
  <b>An unofficial fork of <a href="https://github.com/storytold/photocraft">PhotoCraft</a> with a new native Windows shell.</b><br>
  The PhotoCraft engine (Rust) is the original's, with a short list of additions (below); the interface is rewritten in C++ on Direct3D 11, Direct2D and DirectComposition:<br>
  floating glass docks, physical motion, Phosphor icons and Photoshop-style interaction.
</p>

<p align="center">
  <img alt="Version 0.8.2-v02" src="https://img.shields.io/badge/version-0.8.2--v02-d69e2e?style=flat-square">
  <img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows%2010%2F11-x64-2f7bf5?style=flat-square">
  <img alt="Rust + C++" src="https://img.shields.io/badge/Rust%20%2B%20C%2B%2B-engine%20%2B%20shell-b7410e?style=flat-square">
  <img alt="License: MIT OR Apache-2.0" src="https://img.shields.io/badge/license-MIT%20%2F%20Apache--2.0-3a3a3a?style=flat-square">
</p>

<p align="center">
  <img src="vetro/images/main.jpg" alt="PhotoCraft by Vetro: the photo fills the window, with floating glass docks around it" width="100%">
</p>

The original PhotoCraft README is kept as [README.photocraft.md](README.photocraft.md).

## How it differs from PhotoCraft

| | PhotoCraft (original) | PhotoCraft by Vetro |
|---|---|---|
| Interface | egui / eframe on wgpu (immediate mode), one codebase for macOS, Windows, Linux and the web | A native C++ shell for Windows: Direct3D 11 → Direct2D 1.1 → DirectComposition, with its own retained widget classes |
| Look | A classic Photoshop-like layout with docked panels | The Vetro style: the photo is the backdrop of the whole window, surrounded by floating frosted-glass docks (live blur of the scene, tint, grain, rim light, shadow), Phosphor icons, spring motion, menus that grow out of their button |
| Engine | `crates/*`: layers, brushes, PSD, filters, history, 890+ commands | The same engine (in sync with the original's `main` as of 2026-10-13) plus the additions listed under "Engine additions" below |
| Link to the engine | Direct Rust calls | A C ABI bridge, `crates/bridge` (`photocraft_bridge.dll`): commands by id, document snapshots, live brush strokes, the GPU canvas, the menu catalog and translations |
| Canvas | The wgpu compositor inside the egui window | The engine's own GPU compositor on Direct3D 12 draws into a texture shared with the shell's Direct3D 11 device; only changed areas are redrawn; a CPU path as fallback |
| Colour | The engine's colour management | The window's monitor ICC profile → the engine's canvas table → a Direct2D 3D LUT effect on the canvas |
| Responsiveness | — | Commands, dialog previews and saving run in the background; the interface stays live while the engine works |
| Languages | 20 languages | English and Russian, switched by clicking the logo; Russian comes from the original's catalog, the shell's own strings are translated separately |
| Platforms | macOS, Windows, Linux, FreeBSD, web | Windows 10/11 x64 only (the shell); the engine itself stays cross-platform |

Everything the original has is still here: the egui app (`apps/photocraft`), the CLI, the web build
and the MCP server build as before. Vetro only adds `crates/bridge` and `vetro/`.

## What's new in 0.8.2-v02

The Photoshop-parity plan for the shell (tickets VT-24…VT-85, [vetro/TICKETS.md](vetro/TICKETS.md))
is implemented. Everything is driven by engine commands and checked by native tests that run the
real handlers and read framebuffer pixels, on the CPU path and on the shared GPU texture.

- **Keys and gestures:** arrow-key nudges, Move with auto-select, Alt-copy and 45° constraints,
  brush keys, marquees from the centre, spring-loaded tools, scrubby zoom, rotated view. Input that
  arrives while the engine is busy is queued instead of dropped.
- **The system:** the Windows clipboard (PNG / DIB in and out), dropping files in as layers,
  document tabs (reorder, close, Save / Discard / Cancel), recent files, shortcut editor with
  Photoshop `.kys` import, New Document and Export As.
- **Options bar and Free Transform:** transform menu and numeric fields, reference point, 16 Warp
  styles with grids, repeat transform, align and distribute, shape properties, selection options,
  Eyedropper, Gradient and Crop (with rotation, overlays and Content-Aware fill), Pen details,
  Move by marquee and layer drag onto another tab.
- **Brushes, layers, type:** the brush picker and panels with ABR import, adjustment properties,
  masks, locks, layer effects and smart filters, live retouching (Clone, Healing, Patch,
  Content-Aware Move), on-canvas Unicode type with the Character and Paragraph panels.
- **Rulers, guides, grid and snapping;** a Photoshop-style colour picker; Color, Swatches (ACO /
  ASE), Gradients (GRD) and Patterns (PAT) panels; Channels and Paths; History with snapshots and
  the History Brush; Navigator, Histogram and Info; document properties.
- **Workspaces:** Select and Mask, Quick Mask, screen modes, the contextual task bar, Puppet Warp,
  Vanishing Point, Content-Aware Scale and Fill, Liquify with undo, Guided Upright, shape / path /
  pixel output modes and Stroke Path.
- **Agents in the running app:** an optional local control channel (loopback only, token file, off by
  default) lets the same MCP server drive the window you are looking at; see
  [vetro/CONTROL.md](vetro/CONTROL.md).
- **Language:** the remaining English labels, default layer names and dialog captions are
  translated; dialogs have a close button.

### Engine additions

The engine is the original's; these changes were made for the fork and are covered by tests:

- `image.cropContentAware`: Content-Aware crop fill as one history step.
- Select and Mask output to a layer mask keeps the mask's flags, and a document with both a
  selection and a mask needs an explicit source (`auto`, `selection` or `layerMask`).
- Guided Upright: rotate and perspective-correct an image from horizontal and vertical guides.
- Fixes brought over from the original: layered TIFF export (#5514) and the ICC profile of HEIF grids
  (#5553).
- **Gradient fills keep their stops (#5520).** A gradient fill saved with Smoothness (the default in
  Photoshop for years) used to be opened as 97 baked stops, which made it impossible to edit. It now
  keeps the stops as saved, with the smoothness, interpolation method and opacity midpoints beside
  them (`smoothness`, `method` and `opacity_midpoints` on the fill), and is drawn as before. They are
  written back to PSD and `.pcraft`, and set through `gradient.fill.set` / `gradient.fill.create`;
  the Gradient Editor has a Smoothness field and a Method list.

## What was in 0.8.2-v01

**Shell and style**
- Floating, draggable docks: brand and menus, tool options, the toolbar with flyout groups,
  Properties / Adjustments, Layers / History, the status capsule, zoom.
- One frosted-glass recipe everywhere; springs on everything that moves; menus that morph out of
  their button with a cascade of rows; Reduce Motion collapses the animation.
- Interaction classes instead of one-off drawing code: buttons, slider, toggle, segmented control,
  dropdown list, menu with submenus, tabs, dock, toast, tooltip, colour field, curve editor, and
  dialogs generated from each command's parameter description.

**Canvas**
- The engine's GPU compositor through a shared D3D12 ↔ D3D11 texture, updated by damage; a CPU
  path with detail levels for very large documents.
- Colour management by monitor profile; pan and zoom on springs.

**Tools** (all driven by engine commands)
- Brush, Pencil and Eraser with live engine strokes; smoothing; Alt+wheel and Alt+right-drag set
  size and hardness.
- Retouching: Spot Healing and Healing Brush, Clone Stamp (Alt sets the source, Aligned), Remove,
  Blur, Sharpen, Smudge, Dodge, Burn, Sponge and more.
- Selections: Rectangular and Elliptical Marquee, Lasso, Magnetic Lasso, Quick Selection, Magic
  Wand, Object Selection, with add and subtract modes.
- Crop and Perspective Crop; Pen, Freeform Pen and Curvature Pen; Path and Direct Selection; path
  actions in the right-click menu.
- Shapes: Rectangle, Rounded Rectangle, Ellipse, Triangle, Polygon, Star, Line.
- Horizontal and Vertical Type, Gradient, Paint Bucket, Eyedropper (with its own cursor), Color
  Sampler, Ruler, Note, Count, Artboard.

**Editors and dialogs**
- Free Transform: scale, rotate, distort and a Bézier-patch Warp, previewed live by the engine,
  with Ctrl+Z step by step.
- Camera Raw (tabs, HSL, color grading, point curve), Curves with points, Liquify (on-canvas tools,
  freeze mask), Layer Style, and every engine filter and adjustment as a dialog with live preview.

**Layers**
- Right-click menu, double-click (name: rename; row: layer style), Alt-click between layers for a
  clipping mask, drag to reorder (Alt: copy), visibility, blend modes, opacity.

**Menus and files**
- Photoshop's full menu from the engine's catalog, with availability and shortcuts.
- Open and save (PSD, PNG, JPEG, TIFF, WebP, `.pcraft` and the engine's other formats), system
  dialogs, file drop.
- Context menus on the canvas, for the pen, Free Transform and layers.

The details, with what was verified live, are in [vetro/PLAN.md](vetro/PLAN.md).

<p align="center">
  <img src="vetro/images/warp.jpg" alt="Free Transform Warp: a Bézier patch bent from inside, previewed live by the engine" width="100%">
</p>

## Architecture

```
VetroView — C++          vetro/shell      window, docks, widgets, tools, motion, glass
      ↑↓ C ABI           crates/bridge    photocraft_bridge.dll: commands, snapshots, strokes, GPU canvas
PhotoCraft Engine — Rust crates/*         layers, brushes, PSD, filters, history (unchanged)
      ↓
GPU: Direct3D 11 + Direct2D + DirectComposition (shell), wgpu on Direct3D 12 (canvas)
```

## Build and run

You need Rust (stable) and Visual Studio 2022 with the "Desktop development with C++" workload
(x64); CMake and Ninja come with Visual Studio.

```powershell
vetro\build.ps1 -Run -Open "C:\path\to\photo.jpg"
```

The result is `vetro\dist\PhotoCraftVetro.exe` next to `photocraft_bridge.dll`. A ready build is
on the [Releases](https://github.com/Phnem/photocraft-by-Vetro/releases) page.

## Roadmap

### 1. Photoshop parity in the shell (done in 0.8.2-v02)

The engine already did almost everything the interface lacked, so this was mostly shell work. The
gaps were listed in [vetro/PS_GAPS.md](vetro/PS_GAPS.md) and planned as tickets VT-24…VT-68 in
[vetro/TICKETS.md](vetro/TICKETS.md); VT-69…VT-85 are the defects and upstream issues found while
checking them ([vetro/ISSUES.md](vetro/ISSUES.md)). The original plan, for reference:

1. **Keys and gestures:** arrow-key nudges, Alt-drag copies with Move, number keys for opacity,
   Shift+[ ] for hardness, point-by-point Polygonal Lasso, Layers panel details.
2. **The system:** the Windows clipboard, dropping a file in as a layer, document tabs, recent
   files.
3. **Options bar and Free Transform:** transform menu and numeric fields, reference point, options
   for shapes, selections, Eyedropper, Gradient, Crop and Pen.
4. **Brushes, layers, adjustments:** the brush picker on right-click, the Brushes and Brush Settings
   panels, adjustment layer properties, masks, locks, effects under each layer, smart filters.
5. **Type:** on-canvas editing, fonts, the Character and Paragraph panels.
6. **Rulers, guides, grid and snapping.**
7. **Panels:** a Photoshop-style colour picker, Color, Swatches, Channels, Paths, History with
   snapshots, Navigator, Histogram, Info.
8. **Workspaces:** Select and Mask, Quick Mask, screen modes, the contextual task bar, New Document
   and Export As.

### 2. AI experiments

The plan is in [vetro/AI_PLAN.md](vetro/AI_PLAN.md) (in Russian). A hybrid: local models (ONNX
Runtime on the GPU) for precise masks and retouching, OpenRouter for scene understanding and
generation. AI results are ordinary layers, masks and selections, all non-destructive.

- **Smart selection with suggestions:** people, sky, water, horizon in one click (SAM 2.1).
- **Background and object removal:** BiRefNet, LaMa.
- **Generative replace and expand:** image models through OpenRouter, composited only inside the
  mask, with colour matching.
- **Portrait retouching:** face detection and parsing, skin smoothing that keeps texture.
- **Old photo restoration:** scratches, noise, faces, colorization, upscaling.
- **Text commands** ("make it warmer and lift the shadows"): an LLM plans a sequence of engine
  commands.

### 3. The rest of the engine

The engine has more than 890 commands. The menus already reach all of them; on-canvas and panel
interaction comes one feature at a time. Next after parity: Puppet Warp and Perspective Warp on
the canvas, the Filter Gallery, animation and video layers, slices and asset export, colour lookup
and soft proofing, painting symmetry, tool presets, actions and batch processing, WebAssembly
plug-ins.

## Documents

| File | What it covers |
|---|---|
| [vetro/PLAN.md](vetro/PLAN.md) | What is done: tickets VT-1…VT-23 and what was verified live |
| [vetro/TICKETS.md](vetro/TICKETS.md) | Tickets VT-24…VT-85 with the engine commands they build on, estimates and what was verified |
| [vetro/ISSUES.md](vetro/ISSUES.md) | Which issues of the original repository apply to this fork, with the evidence |
| [vetro/CONTROL.md](vetro/CONTROL.md) | Driving the running app from MCP clients: the local control channel |
| [vetro/PS_GAPS.md](vetro/PS_GAPS.md) | What was missing compared with Photoshop 2026 when the plan was written |
| [vetro/AI_PLAN.md](vetro/AI_PLAN.md) | How to bring in AI features |
| [README.photocraft.md](README.photocraft.md) | The original PhotoCraft README (engine, egui app, CLI, MCP) |
| [AGENTS.md](AGENTS.md), [docs/](docs/) | The engine's rules and documentation, from the original |

The documents in `vetro/` are written in Russian.

## License and credits

- PhotoCraft — © the PhotoCraft authors ([storytold/photocraft](https://github.com/storytold/photocraft)),
  MIT OR Apache-2.0. This fork is distributed under the same terms
  ([LICENSE-MIT](LICENSE-MIT), [LICENSE-APACHE](LICENSE-APACHE), [NOTICE](NOTICE)).
- Icons — [Phosphor Icons](https://phosphoricons.com/), MIT
  ([vetro/shell/third_party/phosphor/LICENSE](vetro/shell/third_party/phosphor/LICENSE)); the
  missing ones are drawn on the same grid.
- PhotoCraft by Vetro is an unofficial fork and is not affiliated with the PhotoCraft or ArtCraft
  teams. The ArtCraft names and marks belong to them and are not used in this fork.
- Photoshop is a trademark of Adobe; it is mentioned only to compare behaviour.
