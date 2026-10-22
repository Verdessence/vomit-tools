# vomit-tools

Soundscape, skybox and particle editors for Source engine games, as one Qt 6 app for Linux and Windows.

**Status:** the soundscape and soundscript editor works, with sound previews. The skybox and
particle editors come next.

## Opening things

Any of these works, and so does dragging files or folders onto the window or passing them on the
command line (`vomit-tools <file|folder|gameinfo.txt>...`):

- **A game:** **File → Open Game** lists the Source games in your Steam libraries, or you can browse
  to any `gameinfo.txt`. The **Game** tab then shows its files as the engine sees them (VPKs and
  folders merged, the winning copy of each file).
- **A folder:** **File → Open Folder** opens a mod folder, a `custom/` folder or anything else in
  the **Folder** tab.
- **A file:** **File → Open File** opens a file. Soundscapes and soundscripts open in the editor,
  under any name (the file name or the entries decide); other files show in the preview.
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
  - **Sound files** in either browser show a waveform with the loop point, and can be played once or looped.
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

## Building

You need CMake 3.25+, a C++20 compiler and Qt 6.5+ (Core, Widgets, Test).
[sourcepp](https://github.com/craftablescience/sourcepp) and [miniaudio](https://miniaud.io) are fetched
at configure time; to build offline, point `FETCHCONTENT_SOURCE_DIR_SOURCEPP` and
`FETCHCONTENT_SOURCE_DIR_MINIAUDIO` at local copies.

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

## Layout

| Path | What |
|---|---|
| `src/core/kv1` | Lossless KeyValues1 parser/writer (soundscapes, VMTs, gameinfo.txt, manifests) |
| `src/core/dmx` | DMX binary encodings 1–2 reader/writer and keyvalues2 text export (PCF files) |
| `src/core/vfs` | gameinfo.txt search paths, VPK and folder mounts, Steam library detection |
| `src/core/sound` | Sound constants, soundscape/soundscript schema, linter, WAV loop points, playback planning |
| `src/core/audio` | Decoding (WAV PCM/ADPCM, MP3) and playback on [miniaudio](https://miniaud.io) |
| `src/app` | Qt app: game and folder browsers, sound editor (cards, value pickers, playback), document tabs |

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
