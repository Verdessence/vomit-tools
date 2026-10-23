# vomit-tools

Soundscape, skybox and particle editors for Source engine games, as one Qt 6 app for Linux and Windows.

**Status:** the soundscape and soundscript editor works, with sound previews; the skybox creator
makes skies from six images; and the particle editor edits PCF files, with a live preview.

## Opening things

Any of these works, and so does dragging files or folders onto the window or passing them on the
command line (`vomit-tools <file|folder|gameinfo.txt>...`):

- **A game:** **File → Open Game** lists the Source games in your Steam libraries, or you can browse
  to any `gameinfo.txt`. The **Game** tab then shows its files as the engine sees them (VPKs and
  folders merged, the winning copy of each file).
- **A folder:** **File → Open Folder** opens a mod folder, a `custom/` folder or anything else in
  the **Folder** tab.
- **A file:** **File → Open File** opens a file. Soundscapes and soundscripts open in the sound
  editor, under any name (the file name or the entries decide), and `.pcf` files in the particle
  editor; other files show in the preview, VTFs and images included.
- **Recent:** **File → Open Recent** lists recent games, folders and files.

A game isn't required. When a file or folder belongs to one (a `gameinfo.txt` sits in it or above
it), that game is mounted automatically, for sound lookups, linting and manifests. Without a game,
sounds are looked for beside the file: for `mymod/scripts/soundscapes_x.txt`, under `mymod/sound/`.

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
  looks the same way in game.
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

## Particle editor

- **Opening:** double-click a `.pcf` in the **Game** or **Folder** tab, open one from **File → Open
  File**, or start one with **File → New Particle File**.
- **Systems:** the file's systems are listed on the left, with child systems under the systems that
  spawn them, or (**Grid**) as pictures of each effect. **New System** starts one with a basic set of operators (it emits, moves, fades and
  draws). **Duplicate**, **Delete** and renaming (double-click or F2) are there too. Renaming also
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
    up and down. Particles locked to a control point move with it.
  - A panel in the corner shows each system's particles against its `max_particles`, orange when
    it's full. **Bounds** shows each system's bounding box and where its particles really are.
  - Play/pause, restart, speeds from 0.1× to 2×, looping and a ground grid. It draws on the graphics
    card where Qt can (**GPU**), in software otherwise.
  - Edits show at once, without starting the effect over.
  - It's an approximation worked out from what each operator's name and settings say, not the
    game's code. Operators it doesn't run yet are greyed out in the list and named under the
    preview; collisions, models and refraction aren't drawn.
- **Properties:** the right column edits the selected system, operator or child link. Numbers, vectors,
  colours (with a picker), on/off values, materials (completed from the game's materials) and child
  systems each get a fitting field. Names in bold differ from the value the game's files use most,
  and right-clicking one resets it.
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
  - operators none of the game's files use, or used as the wrong kind
  - files the manifest doesn't list
- **Saving** keeps everything the editor doesn't show: an unedited file is written back byte for
  byte. Files from a VPK are saved to the write folder, like soundscapes. **Save As** can also convert
  between binary encodings 1 and 2, or write keyvalues2 text for reading and comparing.
- **Manifest:** **Add to Manifest** lists the file in `particles/particles_manifest.txt`.
- **Undo** works for every change.

The editor learns operators and their usual values from the mounted game's own particle files,
so open a game (or a file inside one) to get the full **Add** menu.

## Building

You need CMake 3.25+, a C++20 compiler and Qt 6.5+ (Core, Widgets, Test). With Qt 6.7+ and its
Shader Tools module, the particle preview can also draw on the GPU.
[sourcepp](https://github.com/craftablescience/sourcepp), [miniaudio](https://miniaud.io) and
[stb](https://github.com/nothings/stb) are fetched at configure time; to build offline, point
`FETCHCONTENT_SOURCE_DIR_SOURCEPP`, `FETCHCONTENT_SOURCE_DIR_MINIAUDIO` and `FETCHCONTENT_SOURCE_DIR_STB`
at local copies.

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
| `src/core/texture` | VTF 7.0–7.5 reading and 7.1/7.2 writing, DXT and other formats; image loading on [stb](https://github.com/nothings/stb) |
| `src/core/sky` | Sky faces and their orientation, preview rendering, seam checks, export, loading the game's skies |
| `src/core/particles` | Particle systems in PCF files: reading, editing, copy and paste, operator defaults from the game's files, checks, manifest; the preview's simulation and drawing |
| `src/app` | Qt app: game and folder browsers, sound editor (cards, value pickers, playback), skybox creator, particle editor, document tabs |

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
