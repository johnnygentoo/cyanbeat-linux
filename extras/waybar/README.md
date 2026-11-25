# Waybar

`cyanbeat-remote` takes cmus-remote's flags and prints cmus's `-Q` format, and
`--or-cmus` hands the same arguments to `cmus-remote` whenever CyanBeat isn't
running. So existing cmus modules switch over by changing the command only:

```jsonc
"custom/cmus_play":  { "format": "", "on-click": "~/.local/bin/cyanbeat-remote --or-cmus -p" },
"custom/cmus_pause": { "format": "", "on-click": "~/.local/bin/cyanbeat-remote --or-cmus -u" },
"custom/cmus_stop":  { "format": "", "on-click": "~/.local/bin/cyanbeat-remote --or-cmus -s" },
"custom/cmus_prev":  { "format": "", "on-click": "~/.local/bin/cyanbeat-remote --or-cmus -r" },
"custom/cmus_next":  { "format": "", "on-click": "~/.local/bin/cyanbeat-remote --or-cmus --next" },
```

For a now-playing label, `media.sh` here is a drop-in for a cmus `-Q` script.

CyanBeat is also a normal MPRIS player (`org.mpris.MediaPlayer2.cyanbeat`), so
waybar's built-in `mpris` module, `playerctl`, media keys and KDE Connect work
with it too.

cava needs nothing: it listens to PipeWire's output, so it shows CyanBeat's audio
like any other player's.
