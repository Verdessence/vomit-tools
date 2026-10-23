# Changelog

## Unreleased

### Particle preview
- **Control points go into the file.** Dragging any point but CP0 writes it into the system's **Set
  Control Point Positions** operator, adding one (first in the list) when there is none. Its four
  slots all start on the dragged point, so no other point changes; dragging another point takes a
  slot that repeats one. Square handles mark the points the file places.
- **Snapping:** dragged points snap to the grid while it shows.
- **Undo:** every control point move undoes, CP0 and **Reset CPs** included. Moves that only change
  the view don't mark the file as modified. Hold Ctrl to move a point the file doesn't place in the
  view only.
- **Set Control Point Positions** now measures from the world or from its offset point, as its
  settings say, and places its points from the start.
- **More operators:** Position Along Path Sequential, Position In CP Hierarchy, Position From Parent
  Particles, Position Modify Warp Random, Set child control points from particle positions,
  oscillating_movement and Collision via traces (against the floor under the grid; the preview has
  no map). Also Velocity Random and Position Along Path Random, which no CS:S or HL2 file uses.
  - All 57 functions CS:S and HL2 use now run, so all 288 of their systems run completely.
  - An operator's tooltip says where the preview's version falls short.
- **Functions from other games:** the preview now also runs what TF2's files and custom files made
  with later editors use: Rotation Basic and Rotation Speed Random, Movement Rotate Particle Around
  Axis, Movement Max Velocity, Movement Maintain / Lock to Saved Position Along Path, Noise Scalar,
  Remap Scalar, Remap Distance to Control Point to Vector, Remap Distance Between Two Control
  Points to Scalar, Remap Dot Product to Scalar, Set Control Point To Particles' Center / To Player,
  Color Light from Control Point, Rotation Orient Relative to CP / to 2D Direction, Cull relative to
  model, Move Particles Between 2 Control Points, Remap Control Point to Scalar, Remap Initial
  Distance to Control Point to Scalar, remap scalar to vector, Position Along Ring, Position from
  Parent Cache, Lifetime from Time to Impact, emit noise, turbulent force, Constrain distance to path
  between two control points, Prevent passing through static part of world and
  render_screen_velocity_rotate; and, from the VDC articles, Clamp Scalar, Cull when crossing
  sphere, Lerp Initial Scalar/Vector, Lifespan Minimum Radius/Velocity Decay, Lifespan Maintain
  Count Decay, Normalize Vector, Remap Speed To Scalar, Ramp Scalar Linear Simple, Stop Effect after
  Duration, Inherit Attribute / Initial Value From Parent Particle, Scalar Random, Vector Random,
  Inherit Velocity and Remap Particle Count to Scalar.
- **Older names:** files that use an early editor's names (`basic_movement`, `lifespan_decay`,
  `position_within_sphere`, …) or another case (`Lifetime From Sequence`) run too; setting names
  match without regard to case.
- **Checked against the VDC articles:** a particle's rotation speed now turns it only with Rotation
  Basic; Oscillate counts its frequency over the life or per second as "proportional" says, and its
  start and end as "start/end proportional" says; Movement Lock to Control Point lets go between its
  fade-out times and by distance; operators' "fade oscillate" repeats their fade window.
- Of the 93 particle files on this machine's second drive (TF2's and CS:S custom ones), everything
  runs but what needs the map (Velocity Repulse from World, Cull relative to Ray Trace Environment),
  the control point chain TF2's flamethrower code sets up, and Alpha Fade In/Out Simple, whose
  settings no file shows. An operator's tooltip says why.
- **Models:** **Model** under the preview stands any of the game's models on a control point, as the
  game's effects stand on players and props. It's drawn in both the software and GPU previews,
  textured and lit, and hides the particles behind it.
  - **Position on Model Random** puts particles in its hitboxes (with the hitbox scale, tries and
    direction bias); **Movement Lock to Bone** keeps them there as the model moves. The model stands
    still in its reference pose.
  - **Put a CP on an Attachment** puts a control point where one of the model's attachments is; it
    moves with the model after.
  - **Moving and turning:** drag the model to move it (its control point moves with it, in the view
    only), drag the ring at its feet to turn it (15° steps while the grid shows), or set its pitch,
    yaw and roll under **Model → Turn**. Particles locked to it and points on its attachments turn
    with it, and every move and turn undoes.
  - With **Bounds**, its hitboxes and attachment names show; selecting Position on Model Random shows
    the hitboxes either way. **Frame** takes the model in.
  - Reads MDL versions 44–49 with their `.vvd` and `.vtx`: every CS:S and HL2 model that has
    geometry (3,743 of 3,785; the rest only hold animations).
- **Timeline:** a slider under the preview follows the effect; drag it to go to any moment (it
  pauses). Going back replays from the start, so it shows exactly what playing does.
- **Velocity kept:** placing a particle (Position Within Sphere Random) no longer drops the speed an
  earlier initializer such as Velocity Random gave it.
- **Emission shapes:** the selected position initializer's sphere (with its inner shell and bias),
  box or offset range shows around its control point; with **Bounds**, every one does.

### Particle editor
- **View → Simple Interface** (remembered): names without the underscores and lower case —
  "Max Particles" for max_particles, "Animated Sprites" for render_animated_sprites — and easier
  ways to type values: number boxes with units (s, °, °/s) and sensible steps, control points as
  "CP n", named choices for settings that pick from a few (orientation type, restart behaviour,
  light type, bulge control, collision group…), X/Y/Z boxes for vectors. Advanced settings (when
  an operator runs, visibility tests, the system's simulation tuning) are tucked away while they
  have their usual value. The file is edited exactly the same; turn it off for the files' own keys
  and fields.
- The names, a line on what each function and setting does, and the named values are in
  `src/app/data/lang_en.txt` (Valve's language file layout); how each number is typed is in
  `src/app/data/particle_inputs.txt`. Both are built into the app.
- **Add to a Map's Manifest…** lists the file in `maps/<map>_particles.txt` (made in the write
  folder when the map has none), so only that map loads it.
- In either interface, tooltips give the other name and the explanation, the Add menu finds
  operators by either name, and settings that pick a particle field ("output field") are a list of
  named fields instead of a number.
- The explanations are our own, written from the VDC articles and the preview's behaviour.

### Skybox editor
- **File → Open Sky from Game** lists the skies in a searchable list that fits on the screen,
  instead of a menu that ran off it.
- **Fixed a crash** when picking a sky from that list could not open it: the warning showed while
  the list was still handling the click. What's picked now runs after the click.
- **Cube-map skies:** a sky whose materials only name a cube map (`$envmap`, as WindowImposter skies
  do) opens from that cube map's faces, HDR ones included. The face order and turns were worked out
  from Valve's own cube maps, whose seams then meet exactly.
- **Show Material:** right-click a face to see the material it came from.
- **Fixed another crash picking a sky**, seen on Wayland. Closing the menu could hover **Open Sky
  from Game** again and refill the list while it was still handling the click. Menus that refill
  as they open (skies, recent files, games, the particle editor's **Add** menu, the preview's
  **Model** menu) now delete their old entries after the click is over.
- **Skies that share one texture:** faces that pick their part of a shared texture with proxies
  (TextureTransform over an atlas) now show that part, as the map starts. The texture repeats past
  its edge unless it is clamped.

### Material view
- **Any `.vmt`** picked in the **Game** or **Folder** tab now opens in a material view:
  - **Preview:** the material the way the game draws it, on a flat square or a ball. Drag to move
    the light. Its textures, `$color`, translucency, additive and alpha-tested cut-outs, detail
    textures (every `$detailblendmode` but 10, the detail as a second bump map), bump and
    self-shadowing bump maps, self-illumination, reflections (masked, tinted, with contrast and
    saturation), phong and the Sprite, Modulate, Refract, Water and WindowImposter shaders all
    show.
  - **Proxies run:** animated textures, scrolling, Sine, LinearRamp, the arithmetic proxies and
    TextureTransform play in the preview. Proxies that need the game (PlayerLogo and the like) are
    listed instead.
  - **HDR skies:** an **HDR** switch shows a sky's HDR texture, with an exposure slider. The sun and
    bright clouds show when it's lowered.
  - **Parameters** as the material ends up with them, on a DirectX 9 PC: a patch's include resolved,
    and fallback blocks (`LightmappedGeneric_DX9`, `>=dx90`, …) and conditions applied, with where
    each value came from. The simple interface names them ("Base Texture") and their values
    ("Darken and Lighten").
  - **Textures:** each texture with its size, format and frames. Missing ones are marked.
    Double-click one, or an included material, to open it, then **Back** to return.
  - **Text:** the file itself, with parse diagnostics.
- **Said plainly:** what only the map has (lightmaps, `env_cubemap`, the water's views) gets a
  stand-in, and the view says so, along with the parameters it doesn't show.
- **Checked:** all 10,267 CS:S and HL2 materials read and draw. The only textures that don't load
  are the 369 the game doesn't ship. 135 of the 139 HDR sky faces are as bright as their LDR ones;
  the other four differ in the files themselves.

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
