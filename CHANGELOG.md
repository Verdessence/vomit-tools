# Changelog

## 0.99.0 — The Demo (2026-11-20)

### Demo viewer (new)
- Source demos (`.dem`) open in a tab: summary, timeline to scrub and play, the recorder's
  camera path, console commands and frames.
- The World page draws the demo's map from its own compiled faces and lightmaps, with the
  players as their posed models holding their weapons, shots as tracers and deaths as marks.
  Free, recorder, first-person and chase cameras.
- Chat, kills, HUD messages, menus and rounds in side tabs and, through a player's eyes, in
  the game's own HUD; Tab shows the scoreboard. Sounds, sprays, particles and the map's
  logic guessed from its outputs.
- Voice tab: every stretch of voice chat (Steam voice and CELT), played alone or saved as WAV.
  Players can be muted.
- `vomit-tools-cli demo info|dump|feed|voice|logic`.

### Compiled maps
- A `.bsp` opens in the map viewer, read back into a Hammer map: brushes, displacements,
  entities, static props, overlays and the rest. It shows as compiled (its own faces and
  lighting) and is read-only. `vomit-tools-cli map decompile` writes the VMF.

### Hammer maps
- Load Portal File (Map menu) draws a compile's `.prt` portals; double-click one for its leaves.
- Object Properties shows and edits the selected props' model.
- Loading shows progress, and a map that can't be read says why.

### Also
- File → New… is one window for every kind of new file.
- Views that are loading show placeholders and what they are reading.
- Texture editor: Simple Interface names; Sprite Sheet plays a sequence at its frame durations.
- The material and model picture caches are JPEG XL, much smaller.
- Opening a demo or map uses far less memory: pakfiles are read in place, only the needed
  lumps are loaded.
- README: how to install the VTF thumbnailers for file managers.

## 0.9.0 — The HUD (2026-11-15)

### HUD editor (new)
- Tools > HUD Editor opens the game's HUD, a custom HUD's folder or any layout file, drawn as
  the game draws it (scheme fonts, colours, icons, "#Tokens", sample values) at any screen size.
- Drag, nudge or edit elements, with snapping and guides; anchors kept, lossless, undo.
- Panel files (`resource/ui/*.res`) and the main menu (`GameMenu.res`) open too.
- A scheme panel lists the colours, fonts and borders the selected element uses.

### Scheme editor (new)
- `ClientScheme.res`, `SourceScheme.res` and other schemes: colours, settings, fonts, borders
  and font files edited in place, lossless with undo. Open HUD tabs follow each change.

### Hammer maps
- World Grid (Shift+P): a grid on a plane of its own for the gizmo and the block tool.
- The lighting preview after an edit lights again only what changed, nearest the camera
  first; earlier bakes are kept and reused.
- Sky on Sky Faces toggle; detail prop draw distance setting.
- Any side panel, Face Edit included, can be taken out into its own window.

### Interface
- Each kind of editor can open in a window of its own; a scale for the whole interface.
- Help > About > Licenses lists every third-party library with its license.

## 0.8.0 — The Hammer (2026-11-12)

### Hammer maps (new)
- **Map editor:** `.vmf` maps in a tab laid out as Hammer++'s (3D view plus Top, Front and
  Side), with its keys, colours, grid and camera. Drawn on the graphics card or in software;
  large maps open in about a second.
- **Editing:** move, resize, rotate and shear by handles, a 3D gizmo, G/R/E with typed amounts,
  cut/copy/paste as VMF text, groups, tie to entity, pivot, Transform, Flip, Paste Special.
  Every edit undoes; saving is lossless and keeps a `.vmx`.
- **Tools:** block, entity, clipping, carve, make hollow, vertex, texture application with
  Face Edit and a texture browser, displacements (paint, sew, subdivide, sculpt), sprinkle,
  spline (roads of displacements), physics simulation (Box3D) and a building generator.
- **Object Properties** and **Map Properties** edit keys, flags and outputs with SmartEdit.
- **Reports:** Replace Textures, Find Entities, Entity Report, Check for Problems.
- **Cordons**, **Run Map** (F9, Hammer's compile window and sequences, leaks drawn),
  `vomit-tools-cli map compile`, autosave and recovery, manifests (`.vmm`), Set Up Hammer.
- **Shown:** materials with `$detail` and animated proxies, overlays and decals, the 2D and 3D
  skies, fog, detail sprites, particles, dust motes, FGD helpers, where doors and trains move,
  and a Lighting panel for the map's sun, shadows, tone map and colour correction.
- Import Stripper Config as an undoable edit, grouped in visgroups.
- Tool bars round the views, movable and customizable.

### Shaders (new)
- Compiled shaders (`.vcs` v4–6) read and disassembled; `vomit-tools-cli shader`.
- ps_2_0–ps_3_0 pixel shaders run in software: screenspace_general materials preview in the
  material editor, and over the map's 3D views (Screenspace Shaders panel, on the card as GLSL).

### Settings file (new)
- Settings live in `vomit-tools.nut` (Squirrel tables read as data), written back in place;
  problems show with their line. Keys, tab group rules, per-editor settings and the theme.

### Also
- Skyboxes: Import Panorama (HDRI too, with the HDR set), OpenEXR, sky proxies run.
- Tabs grouped by kind; a shorter File menu; a searchable Recent list.
- Particle browser and live material previews in the browsers; pinned folders in the trees.
- Garry's Mod's `mount.cfg`; GMod playermodel export from ValveBiped models.

## 0.7.0 — The Model (2026-10-27)

- **Models (new):** viewer for `.mdl` and its files (bodies, skins, LODs, sequences), in-place
  editing, exports and glTF import, compiling and packing; a model browser of the whole game;
  Model to VMF (brushes from hulls, cutouts or convex decomposition). Also `vomit-tools-cli model`.
- **Archives:** VPK, GCF, GMA, PAK, WAD, XZP and ZIP open in a tab; `.bsp` shows its pakfile,
  entities and lumps.
- **Sounds:** listen from env_soundscape's positions on a top-down plan; Generate Manifest.
- File windows use the desktop's own chooser on Linux (XDG portal).

## 0.6.6 - qol (2026-10-25)

- Program icon and a splash screen.
- Sound editor: copy and paste entries, blocks and keys.
- Details editor: types, groups and entries as draggable panels; duplicate and copy.
- The open folder and tabs come back at the next start; faster saving; a debug log.

## 0.6.5 — The Detail Sprite (2026-10-24)

- **Details editor (new):** `.vbsp` detail files with lossless saves, undo, checks, a 3D
  preview, and sprite rectangles drawn and packed on the sheet.
- File browsers: right-click menus, Show in File Manager, resizable columns.
- Manifest reminder on save, with Don't remind me again.

## 0.6.0 — The VTF (2026-10-24)

- **Texture editor (new):** `.vtf` with every mip, frame, face and slice; import (frames, cube
  maps, volumes, every format but P8), convert, export, sprite sheets, sprays, batch convert.
- **Material editor (new):** visual or raw editing with live preview and messages; Create VMT.
- **vomit-tools-cli (new)** for textures, and VTF thumbnails in Linux file managers.
- Sound editor: Simple Interface with typed fields.

## 0.5.0 — The Vulkan (2026-10-24)

- **Preferences** (Ctrl+,): Interface, Graphics and Particles.
- **Every preview draws on the graphics card** through Qt's RHI (Vulkan, OpenGL, Direct3D),
  matching the software renderers; software is the fallback.
- **Particles:** every built-in function with the game's settings and defaults (from the SDK's
  data tables), the game's older names, more operators (all CS:S and HL2 systems run), models
  in the preview, a timeline, control points written into the file, and a Simple Interface.
- **Material view:** any `.vmt` previewed as the game draws it, proxies running, with its
  resolved parameters and textures.
- Skybox fixes: cube-map skies, shared-texture faces, crashes picking a sky.

## 0.4.0 — The Particle (2026-10-23)

- **Particle editor (new):** open, edit and save `.pcf` files: systems, operators, properties,
  copy and paste, search across the game, checks, lossless saving and undo.
- **Particle preview:** sprites, trails and ropes with the game's materials, draggable control
  points, limits, bounds and playback controls, on the GPU where available.
- The code follows the Source SDK's naming conventions.

## 0.3.0 — The Sky (2026-10-23)

- **Skybox creator:** six faces on a cross, a preview from inside, checks and fixes, export to
  VTF and VMT.
- Sound previews with a playhead; Help → About; Windows and Linux builds from tags.
