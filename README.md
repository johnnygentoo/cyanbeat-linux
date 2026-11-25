# CyanBeat for Linux

A local music player in the JohnnyCyan look: a library, a column browser and a
now-playing display, in the spirit of mid-2000s desktop jukeboxes, kept light.
Desktop sibling of the CyanBeat Android app, from JohnnyGentoo Studios.

Planned: two-way phone sync over ADB (USB or wireless) or MTP.

## Status

Early (v0.1.0). Working now:

- Library from your folders (`~/Music` by default), tags via TagLib; untagged files
  (yt-dlp rips) show their file name. Stored in SQLite, so launch is instant and
  rescans only re-read changed files.
- Playlists: create (Ctrl+N, or from a selection), rename inline, delete, drag songs
  onto them, drag to reorder, Delete to remove. A song whose file is missing keeps
  its place and reappears when the file comes back.
- Smart lists: Recently Added, Recently Played, Top 25 Most Played.
- Star ratings (click the stars), play counts (counted at half the song or 4 minutes),
  Date Added (when the file landed on disk), Last Played. Right-click the column
  header to show/hide columns.
- Drag songs out of the list into a file manager.
- Equalizer (Window › Equalizer, Ctrl+E): the Android app's 8 bands and presets,
  your own presets, a live response curve. Boosts are real; a limiter switches on
  only while a band is boosted, so flat is untouched audio.
- Visualizer (View › Show Visualizer, Ctrl+T, and faintly behind the now-playing
  display): runs **your cava** on CyanBeat's own audio (after the EQ, in step with
  what you hear), using the smoothing/eq/colours from `~/.config/cava/config`. It's
  started as plain `cava`, so a waybar script that does `pkill -f "cava -p"` won't
  touch it. Without cava there's a built-in analyzer.
- Find Duplicates (Tools menu): identical files, the same YouTube video saved twice
  (`[id]` and `-id` names), and the same song under different names (artist/title in
  any order, track numbers and "(Official Video)" ignored, lengths must match). The
  worse copy is pre-checked by real sound quality (codec, bitrate, lossless, cut-off
  downloads) or by size. Removal goes to the Trash; plays, ratings and playlists move
  to the copy you keep. `cyanbeat --list-duplicates` prints the same report.
- MPRIS (`org.mpris.MediaPlayer2.cyanbeat`): media keys, `playerctl`, waybar's
  `mpris` module and KDE Connect see it. Launching it again raises the open window.
- `cyanbeat-remote`: cmus-remote's flags and `-Q` output, plus `--or-cmus` to fall
  back to cmus when CyanBeat isn't running. See `extras/waybar/`.
- Genre / Artist / Album column browser, search, sortable track list.
- GStreamer playback: mp3, flac, ogg/opus, m4a/aac, wav, wma, webm/mka. Never opens
  a video window. Unplayable files are skipped.
- Now-playing display with a scrubbable progress bar (click the time on the right
  to switch between remaining and total).
- Shuffle, repeat (off / all / one), volume, window layout, all remembered.

## Roadmap

1. ~~Skeleton, theme, playback, library, browser~~
2. ~~SQLite collection: playlists, play counts, ratings, smart lists~~
3. ~~MPRIS + `cyanbeat-remote` (cmus-remote-compatible flags) for waybar~~
4. ~~Visualizer (cava) and EQ with the Android app's presets~~
5. Phone sync over ADB: flagged folders, merged both ways (new files on either side
   are copied to the other; nothing is deleted except in mirror mode, which asks first)
6. MTP fallback, gapless, ReplayGain, tray icon

## Build

Needs Qt 6.5+ (Widgets, Concurrent, Svg, Sql with the SQLite driver, DBus), GStreamer 1.x with
base/good plugins and pbutils
(and libav or ugly for wma/aac), TagLib 2, CMake 3.21+.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/cyanbeat
```

Tests: `CYANBEAT_TEST_FILE=/path/to/song.mp3 ctest --test-dir build`

Install: `cmake --install build --prefix ~/.local` (installs `cyanbeat` and
`cyanbeat-remote`, the desktop entry and the icon).

## Remote

```sh
cyanbeat-remote -u          # toggle pause      -p play   -s stop
cyanbeat-remote -n / -r     # next / previous   -S shuffle  -R repeat
cyanbeat-remote -v +5%      # volume: 50, 50%, +5%, -5%
cyanbeat-remote -k +10      # seek: 90, +10, -10, 1:30, +1m
cyanbeat-remote -Q          # status, in cmus-remote's format
cyanbeat-remote --or-cmus -u   # CyanBeat if it's running, else cmus
```

## Keys

| Key | Action |
|---|---|
| Space | Play / pause |
| Ctrl+→ / Ctrl+← | Next / previous |
| Ctrl+. | Stop |
| Ctrl+↑ / Ctrl+↓ | Volume |
| Ctrl+L | Go to the current song |
| Ctrl+F | Search |
| Ctrl+B | Show/hide the column browser |
| Ctrl+T | Visualizer (Esc or double-click to go back) |
| Ctrl+E | Equalizer |
| Ctrl+O | Add a folder |
| Ctrl+N / Ctrl+Shift+N | New playlist / from selection |
| Delete | Remove selected songs from the playlist |
| F2 | Rename the selected playlist |
| Ctrl+R | Rescan |

## License

CyanBeat is free software under the GNU GPL, version 3 or later; see `LICENSE`.

The Konso typeface in `data/fonts/` is © JohnnyGentoo Studios and is bundled with
CyanBeat. You're welcome to use it.
