# vomit-tools

Soundscape, skybox, particle, texture and material editors for Source engine games, as one Qt 6
app for Linux and Windows, plus a command-line texture tool.

**Status:** the soundscape and soundscript editor works, with sound previews; the skybox creator
makes skies from six images; the particle editor edits PCF files, with a live preview; and the
texture editor makes, converts and inspects VTFs, with a VMT editor beside it.

## Opening things

Any of these works, and so does dragging files or folders onto the window or passing them on the
command line (`vomit-tools <file|folder|gameinfo.txt>...`):

- **A game:** **File → Open Game** lists the Source games in your Steam libraries, or you can browse
  to any `gameinfo.txt`. The **Game** tab then shows its files as the engine sees them (VPKs and
  folders merged, the winning copy of each file).
- **A folder:** **File → Open Folder** opens a mod folder, a `custom/` folder or anything else in
  the **Folder** tab.
- **A file:** **File → Open File** opens a file. Soundscapes and soundscripts open in the sound
  editor, under any name (the file name or the entries decide), `.pcf` files in the particle
  editor, `.vtf` files in the texture editor and `.vmt` files in the texture editor of the texture
  they use (or a material editor of their own when it isn't found); other files
  show in the preview, images included.
- **Recent:** **File → Open Recent** lists recent games, folders and files.

A game isn't required. When a file or folder belongs to one (a `gameinfo.txt` sits in it or above
it), that game is mounted automatically, for sound lookups, linting and manifests. Without a game,
sounds are looked for beside the file: for `mymod/scripts/soundscapes_x.txt`, under `mymod/sound/`.

## Preferences

**Edit → Preferences** (Ctrl+,) holds the app's settings. Changes take effect at once, except the
graphics API, which waits for the next start.

- **Interface:** the simple or regular interface (also under **View → Simple Interface**), and
  whether files opened from outside join the window already open (the default) or start another.
- **Materials:** whether materials are edited as a list of parameters or as text, and whether the
  preview redraws at each edit in the list.
- **Graphics:** what the sky, material and particle previews and the particle grid draw with.
  **Automatic** tries Vulkan first, then the platform's usual API (Direct3D 11 on Windows, OpenGL
  on Linux). Vulkan, OpenGL, Direct3D 11/12 and **Software** (the CPU) can be picked directly. The
  page says what is in use (**Help → About** says it too). If the graphics card fails, the previews
  draw in software.
- **Particles:** whether every system in the grid plays, or only the selected one.

## Sound editor

- **Cards:** each entry shows as a card, with its name on a tab and its keys in a grid. Sub-blocks
  (`playlooping`, `playrandom`, `rndwave`, …) are nested cards. Use **+** to add keys and blocks,
  ✕ or Delete to remove them, and double-click a name to rename it.
- **Value pickers:** each value cell offers what fits its key.
  - `CHAN_*`, `VOL_NORM`, `PITCH_*`, `SNDLVL_*`, `ATTN_*` constants, each listed with what it means.
  - DSP presets by name.
  - Soundscape positions, including `random` in `playrandom`.
  - Other soundscapes' names.
  - A sound-file search over everything the game can load.
- **Meaning column:** the grey column next to each value explains it: `SNDLVL_GUNFIRE` → "140 dB ·
  attenuation 0.2222", `dsp 6` → "Tunnel Medium", `95, 105` → "random from 95 to 105".
- **Linter:** the panel at the bottom checks as you type, and the offending rows are tinted. It catches:
  - malformed values
  - unknown keys
  - missing sound files
  - looping sounds whose WAV has no loop points
  - `playsoundscape` names that don't exist, and loops between soundscapes
  - names defined twice
  - files the manifest doesn't list
- **Previews:**
  - **▶ on a card's tab** plays that block with its settings: a sound entry with its volume, pitch and
    random picks, a `playlooping` looping from its loop point, a `playrandom` once.
  - **▶ on a soundscape** (or **Play** in the toolbar, Ctrl+Enter) runs the whole soundscape: loops,
    random sounds at their intervals, nested soundscapes with their volume and position overrides.
    Positions, distance falloff and DSP aren't simulated; `random` positions are panned instead.
  - **▶ beside a wave** plays the file as it is.
  - **Sound files** in either browser show a waveform with the loop point, and can be played once or
    looped. A playhead follows playback; click the waveform to play from that point.
  - **One sound at a time:** starting a preview stops whatever was playing.
  - **Status bar:** stop and volume controls.
- **Lossless saving:** saving changes only what you edited; comments, spacing and line endings
  are kept byte for byte. Undo and redo work the same way.
- **Where files go:** loose files are saved in place. Files that came from a VPK, and new files
  (**File → New Soundscape/Soundscript File**), go to the game's write folder, `custom/vomit-tools/` by
  default (**Game → Set Write Folder**). The game scans `custom/` at startup.
- **Manifest:** **Add to Manifest** lists a file in `soundscapes_manifest.txt` or
  `game_sounds_manifest.txt`. It edits the manifest the game uses, or writes an overriding copy
  that keeps all the stock entries.

To hear a soundscape in game: restart the game, load a map, then `sv_cheats 1` and
`playsoundscape <name>`. For soundscripts, `playgamesound <name>`.

## Skybox creator

- **Starting:**
  - **File → New Skybox** for a new sky.
  - **File → Open Sky from Game** for any sky the game has. Double-clicking a `materials/skybox/` file
    in the **Game** tab opens its sky too.
  - **Opening one face file** opens its whole sky. That's any of `sky_x_rt.vtf`, `.vmt` or an image,
    whether dropped, opened from **File → Open File**, or double-clicked in the **Folder** tab. The
    other faces are found beside it, and materials follow their `$basetexture`. A file counts as a sky
    face when it sits in a `skybox` folder or at least three of its faces are there.
  - **Export** then writes back to the folder the sky came from, after asking before replacing files.
- **Faces:** drop six images on the cross, or use **Load Faces…**. Files named `…rt`, `…bk`, `…lf`,
  `…ft`, `…up` and `…dn` go to their face by themselves. PNG, JPEG, TGA, BMP, PSD, HDR and VTF are
  read. Right-click a face to turn or mirror it.
- **The cross** is laid out the way the faces join in game: rt (east) in the middle, up above it, dn
  below it, and going right from rt: ft, lf, bk.
- **Preview:** the sky from inside, the way a player sees it, in a square view. Drag to look around, scroll to zoom. The
  angles are Source's: the readout (`setang -20 30 0`) typed into the console (with `sv_cheats 1`)
  looks the same way in game. It draws on the graphics card (see **Preferences**).
- **Checks:**
  - missing faces
  - faces the game can't use (not square, or sides that aren't a power of two)
  - faces that don't meet their neighbours
  - up and down faces turned the wrong way, which other tools often produce. Double-click the check to
    turn the face right.
- **Export** (Ctrl+S) writes `materials/skybox/<name><face>.vtf` and `.vmt` for all six faces to the
  write folder; **Export To…** writes them anywhere.
  - Format: uncompressed BGR888 (no banding) or DXT1 (a sixth of the size).
  - Face size: anything from 256 to 4096. Mipmaps are optional.
  - Then, in game: `sv_skyname <name>`.
- **Test Pattern:** six labelled faces for checking a sky in game. Each edge names the face that
  should be across it, and `setang 0 0 0` faces RT.

The faces' orientation comes from the game's own skies. In all of them, the pixels on either side of
each cube edge match under it, and the sun drawn in a map's sky sits where its `light_environment`
puts the sun.

## Material view

Picking a `.vmt` in the **Game** or **Folder** tab shows the material as the game would draw it,
beside its parameters, its textures and its text. Right-clicking a face in the skybox creator and
choosing **Show Material** shows that face's material.

- **Preview:** a flat square or a ball, lit by a light you drag, drawn on the graphics card at the
  full size of the view (see **Preferences**). It shows:
  - textures, tints and blending (translucent, additive, alpha-tested)
  - detail textures in every blend mode but 10 (the detail as a second bump map)
  - bump maps, including self-shadowing ones
  - self-illumination, reflections and phong
  - the Sprite, Modulate, Refract, Water and WindowImposter shaders
- **Animation:** proxies that only need time run (animated textures, scrolling, Sine and the
  arithmetic ones).
- **HDR skies:** a switch shows the HDR texture, with an exposure slider.
- **Stand-ins:** what only a map has (lightmaps, `env_cubemap`, the water's reflection) gets a
  plain stand-in. Under the preview, the view lists what it can't show.
- **Parameters:** what the material ends up with on a DirectX 9 PC. A patch's include is followed,
  and fallback and conditional blocks are applied. The simple interface names the parameters.

## Texture editor

`.vtf` files open in it, with the material of the same name; **File → Create VTF from Pictures**
makes one; a picture's preview offers **Create VTF…**. Everything is one undo step, and a new
texture asks where to save.

- **Viewing:** every mipmap (optionally blown up to full size), frame (played at a set rate), cube
  face (or all six unwrapped as a cross) and volume slice; R, G, B and A on their own; zoom, pan
  and 3×3 tiling. Beside it: the header, thumbnail and resources, the sprite sheet in full, and the
  flags to tick.
- **Import:** pictures as one image, animation frames (a GIF's or APNG's frames too), a cube map
  (faces named `…rt`, `…lf`… sort themselves), a cube map from a panorama/HDRI, or a volume.
  Every format but P8, versions 7.1–7.5, presets (diffuse, normal map, decal, particle, UI, sky
  faces, distance field, and your own), a separate format for pictures with alpha, Kaiser /
  Mitchell / triangle / box mipmaps, power-of-two, multiple-of-4 or exact sizes, normal maps from height,
  `$distancealpha`, and HDR pictures kept as light values.
- **Convert** writes it again with other choices; **Properties** changes the version,
  bump scale, reflectivity, thumbnail and resources (KVD text, LOD, sheets) without touching the
  pictures.
- **Sprite Sheet** packs frames into a particle sheet like mksheet, reading and writing `.mks`,
  and can cut an existing sheet back into its frames to change it.
- **Export** saves the picture shown, or every frame, face and slice; **File → Convert a Folder of
  Textures** converts whole folders either way.
- **Sprays:** the side panel says whether the game would take it as a spray, and why not; **Make
  Spray** makes it fit (under 512 KB, at most 4096 on a side, no mipmaps; smaller, then fewer frames).
- **Material:** **Texture | Material** at the end of the toolbar turns the side panel to the
  texture's material, drawn in the same viewer, flat or on a ball, right-drag moving the light
  (**As Material** does that on the texture's side too). Untick a parameter on the left to see the preview without it. **Create VMT** starts one (shader, normal map, alpha as solid, blended,
  cut out or added, two-sided, reflections, glow, phong, surface property, keywords). Edit it as a
  list (each value with a box that suits it; right-click or **Add Parameter…** for more, or a proxy)
  or as coloured text (Preferences → Materials, or the switch above it). **Messages** below lists
  mistakes, misspelt or unused parameters, missing textures and alpha the texture can't back up,
  by line. The preview uses the texture as edited, saved or not; **Save** writes what changed.
- Pictures: PNG, APNG, JPEG, TGA, BMP, PSD, GIF, HDR, PFM, QOI and PNM, plus whatever Qt's image
  plugins handle (WebP, JPEG XL, AVIF, EXR, TIFF…).

`vomit-tools-cli` does the same from the command line (`create`, `edit`, `extract`, `info`,
`thumbnail`, with `--watch` to rebuild when the pictures change); `vomit-tools-cli --help` lists the
options. On Linux it also provides VTF thumbnails for GNOME Files, Thunar, Nemo and Caja, and a
plugin draws them in Dolphin when KDE Frameworks 6 is found at build time.

## Detail props editor

`detail.vbsp` (or a map's own detail file) opens in it: detail types on the left, their groups and
entries as cards (each sprite shown beside its fields), the sprite sheet on the right with every
rectangle drawn (drag the picked one to move or resize it), and a **3D preview** that scatters the
type on its ground material. Checks cover amounts, alphas, rectangles, missing models and which
materials use each type. **Pack pictures…** builds a new sheet; undo covers everything.

## Particle editor

- **Opening:** double-click a `.pcf` in the **Game** or **Folder** tab, open one from **File → Open
  File**, or start one with **File → New Particle File**.
- **Systems:** the file's systems are listed on the left, with child systems under the systems that
  spawn them, or (**Grid**) as pictures of each effect. The selected system's picture plays;
  **Preferences** can make every system in view play. **New System** starts one with a basic set of
  operators (it emits, moves, fades and draws). **Duplicate**, **Delete** and renaming (double-click or F2) are there too. Renaming also
  updates the child links and `cull_replacement_definition` values that name the system.
- **Operators:** the middle column shows the selected system's renderers, operators, initializers,
  emitters, child systems, forces and constraints, each list in the order it runs. **Add** offers
  every function the game's own particle files use, most used first, in a searchable list. ▲ and ▼
  reorder, and **Remove** takes one out.
- **Preview:** the selected system plays above its properties, with the systems it spawns, using the
  game's materials and sprite sheets.
  - Drag to turn around it, right-drag to move, scroll to zoom, double-click (or **Frame**) to fit it
    in view.
  - The control points the system uses are handles: drag one to move it, with Shift held to move it
    up and down. They snap to the grid while it shows. Particles locked to a control point move
    with it.
    - CP0 is where the effect is, so moving it only changes the view.
    - Moving any other point writes it into the file with a **Set Control Point Positions** operator,
      added when the system has none; square handles mark the points the file places. Hold Ctrl to
      move a point the file doesn't place in the view only.
    - Moves undo like any other edit. **Reset CPs** puts the view's points back.
  - A panel in the corner shows each system's particles against its `max_particles`, orange when
    it's full. **Bounds** shows each system's bounding box and where its particles really are.
  - The selected position initializer's sphere, box or offset range shows around its control
    point; with **Bounds**, every one does.
  - **Model** stands one of the game's models on a control point, for effects made for players
    and props: Position on Model Random starts particles in its hitboxes, and control points can be
    put on its attachments. The model is drawn, hides what's behind it, and stays in its reference
    pose.
  - Play/pause, restart, speeds from 0.1× to 2×, looping, a ground grid, and a timeline to drag
    to any moment. It draws on the graphics card (see **Preferences**).
  - Edits show at once, without starting the effect over.
  - It's an approximation worked out from what each operator's name and settings say, not the
    game's code. Operators it doesn't run yet are greyed out in the list and named under the
    preview. Collisions hit only the floor under the grid, and refraction isn't drawn.
- **Simple interface** (View → Simple Interface, or **Preferences**): tidy names ("Max Particles", "Animated Sprites"),
  number boxes with units, named choices and X/Y/Z boxes, and advanced settings tucked away until
  changed. Off, the files' own keys and fields show. The file is edited the same either way. The
  text is in `src/app/data/lang_en.txt`.
- **Layout:** the systems on the left; the preview and the properties of what is selected in the
  middle; the selected system's emitters, initializers, operators, renderers, forces, constraints
  and child systems on the right.
- **Properties:** the panel under the preview edits the selected system, operator or child link.
  Every setting the game reads is listed, in the game's order; those the file leaves out show in
  italics at the game's default, and changing one adds it to the file. Numbers, vectors, colours
  (with a picker), on/off values, materials (completed from the game's materials), particle fields
  (only the kind the setting takes) and child systems each get a fitting field. Names in bold
  differ from the game's default, and right-clicking one resets it. Settings the game doesn't read
  are listed apart.
- **Search:** **Edit → Find Particle System** (Ctrl+Shift+F, or **Search All Files**) looks through
  every particle file the game has, by system name, material, operator or child system, and opens
  the one picked.
- **Copy and paste:** copied systems bring the systems they spawn along, and paste into any particle
  file, in the same window or another one. The clipboard also gets the systems as keyvalues2 text.
- **Checks:**
  - systems that spawn themselves
  - child links to nothing
  - names used twice in the file
  - names another of the game's files also defines
  - materials the game doesn't have
  - cull replacements that don't exist
  - functions the game doesn't build in and none of its files use, or used as the wrong kind
  - settings a function doesn't read, or of a type it doesn't read them as
  - files the manifest doesn't list
- **Saving** keeps everything the editor doesn't show: an unedited file is written back byte for
  byte. Files from a VPK are saved to the write folder, like soundscapes. **Save As** can also convert
  between binary encodings 1 and 2, or write keyvalues2 text for reading and comparing.
- **Manifest:** **Add to Manifest** lists the file in `particles/particles_manifest.txt`.
- **Undo** works for every change.

The **Add** menu lists every function the game builds in, with every setting at the game's
default. They come from the data tables in the Source SDK 2013's particle library, which CS:S's
client shares. Functions from other games' files (TF2's, or later editors') are learned from the
mounted game's own particle files.

## Building

You need CMake 3.25+, a C++20 compiler and Qt 6.5+ (Core, Widgets, Network, Test). With Qt 6.7+ and its
Shader Tools module, the previews can also draw on the graphics card, through Qt's RHI (Vulkan,
OpenGL, Direct3D or Metal); otherwise they draw in software.
[sourcepp](https://github.com/craftablescience/sourcepp), [miniaudio](https://miniaud.io) and
[stb](https://github.com/nothings/stb) are fetched at configure time; to build offline, point
`FETCHCONTENT_SOURCE_DIR_SOURCEPP`, `FETCHCONTENT_SOURCE_DIR_MINIAUDIO` and `FETCHCONTENT_SOURCE_DIR_STB`
at local copies.
Vulkan builds use an installed Vulkan-Headers package or fetch pinned headers automatically;
`FETCHCONTENT_SOURCE_DIR_VULKAN_HEADERS` can point at an offline copy. A Vulkan SDK is not needed.

### Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/src/app/vomit-tools
```

### Windows (MSVC)

With Visual Studio 2022 and Qt for `msvc2022_64`:

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

`dist` is then self-contained: `cmake --install` runs `windeployqt`.

### Windows, cross-compiled from Linux

Use [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) with Qt's `llvm_mingw` Windows build and a
Linux Qt of the same version for the host tools. Instructions are in
[cmake/toolchains/llvm-mingw-x86_64.cmake](cmake/toolchains/llvm-mingw-x86_64.cmake).

## Code style

The code follows the naming and layout conventions of Valve's Source SDK (the style, not its code):

- Classes are `CName`, plain structs and enums `Name_t`, enum values `ENUM_NAME_VALUE`.
- Members are `m_` plus a type prefix: `m_nCount` (integers and enums), `m_flTime` (floats),
  `m_bVisible`, `m_pEditor` (pointers), `m_szName` (strings), `m_vecOrigin`, `m_pfnRead` (callables),
  and `m_Name` for other types. Locals and parameters use the same prefixes without `m_`.
- Functions are `PascalCase`; accessors are `GetName()`, `IsModified()`, `SetName()`.
- Every function definition has a `Purpose:` banner, and every file a header block.
- Layout is set by `.clang-format`: Allman braces, tabs, `if ( x )`, `Call( a, b )`, `p[ i ]`.

## Layout

| Path | What |
|---|---|
| `src/core/kv1` | Lossless KeyValues1 parser/writer (soundscapes, VMTs, gameinfo.txt, manifests) |
| `src/core/dmx` | DMX binary encodings 1–2 reader/writer and keyvalues2 text export (PCF files) |
| `src/core/vfs` | gameinfo.txt search paths, VPK and folder mounts, Steam library detection |
| `src/core/sound` | Sound constants, soundscape/soundscript schema, linter, WAV loop points, playback planning |
| `src/core/audio` | Decoding (WAV PCM/ADPCM, MP3) and playback on [miniaudio](https://miniaud.io) |
| `src/core/texture` | VTF 7.0–7.5 reading and writing in every format but P8 (frames, cube maps, volumes, resources), making textures from pictures, sprite sheets, image files on [stb](https://github.com/nothings/stb) |
| `src/core/sky` | Sky faces and their orientation, preview rendering, seam checks, export, loading the game's skies |
| `src/core/material` | Materials (VMT): parameters after patches and fallbacks, proxies, and the preview's drawing |
| `src/core/particles` | Particle systems in PCF files: reading, editing, copy and paste, the game's built-in functions and their defaults, checks, manifest; the preview's simulation and drawing |
| `src/app` | Qt app: game and folder browsers, sound editor (cards, value pickers, playback), skybox creator, material view, particle editor, document tabs, preferences |
| `src/cli` | `vomit-tools-cli`, the texture tools without Qt, and the Linux thumbnailer files |
| `src/thumbnailer` | The KDE (Dolphin) thumbnail plugin |
| `src/app/shaders` | The previews' shaders for the graphics card (sky, material, particles), each matching its software renderer in `src/core` |

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
