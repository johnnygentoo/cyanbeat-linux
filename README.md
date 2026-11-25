# CyanBeat for Linux

A local music player in the JohnnyCyan look: a library, a column browser and a
now-playing display, in the spirit of mid-2000s desktop jukeboxes, kept light.
Desktop sibling of the CyanBeat Android app, from JohnnyGentoo Studios.

Planned: two-way phone sync over ADB (USB or wireless) or MTP, waybar / MPRIS
control, and a cava visualizer.

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
- Genre / Artist / Album column browser, search, sortable track list.
- GStreamer playback: mp3, flac, ogg/opus, m4a/aac, wav, wma, webm/mka. Never opens
  a video window. Unplayable files are skipped.
- Now-playing display with a scrubbable progress bar (click the time on the right
  to switch between remaining and total).
- Shuffle, repeat (off / all / one), volume, window layout, all remembered.

## Roadmap

1. ~~Skeleton, theme, playback, library, browser~~
2. ~~SQLite collection: playlists, play counts, ratings, smart lists~~
3. MPRIS + `cyanbeat-remote` (cmus-remote-compatible flags) for waybar
4. Visualizer (cava) and a 10-band EQ with the Android app's presets
5. Phone sync over ADB: flagged folders, merged both ways (new files on either side
   are copied to the other; nothing is deleted except in mirror mode, which asks first)
6. MTP fallback, gapless, ReplayGain, tray icon

## Build

Needs Qt 6.5+ (Widgets, Concurrent, Svg), GStreamer 1.x with base/good plugins
(and libav or ugly for wma/aac), TagLib 2, CMake 3.21+.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/cyanbeat
```

Tests: `CYANBEAT_TEST_FILE=/path/to/song.mp3 ctest --test-dir build`

Install: `cmake --install build --prefix ~/.local`

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
| Ctrl+O | Add a folder |
| Ctrl+N / Ctrl+Shift+N | New playlist / from selection |
| Delete | Remove selected songs from the playlist |
| F2 | Rename the selected playlist |
| Ctrl+R | Rescan |

## License

CyanBeat is free software under the GNU GPL, version 3 or later; see `LICENSE`.

The Konso typeface in `data/fonts/` is © JohnnyGentoo Studios and is bundled with
CyanBeat. You're welcome to use it.
