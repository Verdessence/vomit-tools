# Changelog

## 0.4.0 — The Particle (2026-10-23)

The particle editor arrives: open, edit and save the game's `.pcf` files, with a live preview.

### Particle editor
- **Opening:** double-click a `.pcf` in the Game or Folder tab, open it from **File → Open File**, drop
  it on the window, or start one with **File → New Particle File**.
- **Systems:**
  - Listed with child systems under the systems that spawn them, or as a **Grid** of pictures of
    each effect.
  - **New System** starts one with a basic set of operators. **Duplicate** and **Delete** are there
    too, and double-click or F2 renames.
  - Renaming also updates the child links and cull replacements that name the system.
- **Operators:** the selected system's renderers, operators, initializers, emitters, child systems,
  forces and constraints, each list in the order it runs.
  - **Add** offers every function the game's own particle files use, most used first, in a
    searchable list.
  - ▲ ▼ reorder, and **Remove** takes one out.
- **Properties:** a fitting field for each value: numbers, vectors, colours with a picker, on/off
  values, materials completed from the game's, and child systems. Names in bold differ from the
  game's usual value, and right-click resets them.
- **Copy and paste** between files, bringing the systems a system spawns along; the clipboard also
  gets keyvalues2 text.
- **Search:** **Edit → Find Particle System** (Ctrl+Shift+F) searches every particle file the game
  has by system name, material, operator or child system, and opens the result.
- **Checks:**
  - systems that spawn themselves
  - child links to nothing
  - names used twice
  - names another game file also defines
  - missing materials
  - cull replacements that don't exist
  - operators the game never uses, or uses as another kind
  - files the manifest doesn't list
- **Saving:**
  - An unedited file is written back byte for byte.
  - Files from a VPK save to the write folder.
  - **Save As** converts between binary encodings 1 and 2, or writes keyvalues2 text.
  - **Add to Manifest** lists the file in `particles/particles_manifest.txt`.
- **Undo** for every change.

### Particle preview
- **What plays:** the selected system and the systems it spawns, with the game's materials and
  sprite sheets.
  - Sprites can face the camera, stand upright, or lie flat like decals (`orientation_type` 0, 1, 2).
  - Sprite trails and ropes are drawn too.
  - Blending is additive or alpha, with overbright and add-self.
- **Camera:** drag to orbit, right-drag to pan, scroll to zoom, double-click or **Frame** to fit.
- **Control points:** the ones the system uses are handles. Drag one to move it; hold Shift to move
  it up and down. Particles locked to a control point follow it.
- **Particle limits:** a panel shows each system's particles against its `max_particles`, orange
  when it's full.
- **Bounds:** shows each system's bounding box next to where its particles really are.
- **Playback:** play/pause, restart, speeds from 0.1× to 2×, looping and a ground grid.
- **Live edits:** changes show at once, without starting the effect over.
- **GPU:** draws on the graphics card where Qt 6.7+ and its Shader Tools are available, in software
  otherwise.
- **Coverage:** 48 of the 57 operator functions CS:S and HL2 use are simulated. The rest are greyed
  out and named under the preview, and collisions, models and refraction aren't drawn. The
  simulation is worked out from each operator's name and settings, so it's close, not exact.

### Everywhere
- A footer in the status bar shows the edition, version and operating system.
- The code now follows the Source SDK's naming and layout conventions (see **Code style** in the
  README), with a `.clang-format` for the layout.

### Known issues
- Particle files from games after the 2013 branch (DMX binary encodings 3–5) can't be opened yet.
- An edited particle file hasn't been checked in game yet.

## 0.3.0 — The Sky (2026-10-23)

- **Skybox creator:**
  - Six faces on a cross laid out the way they join, and a square preview from inside with the
    game's `setang` angles.
  - Checks for missing faces, unusable sizes and mismatched seams, plus a one-click fix for turned
    up and down faces.
  - Export to VTF and VMT, and a labelled test pattern.
- **Opening skies:** any sky the game has, or a sky saved as files, by opening any one of its faces.
- **Sound previews:** a playhead on the waveform and click-to-play from any point. Only one preview
  plays at a time.
- **Help → About** shows the version and build.
- Windows and Linux builds are published from tags.
