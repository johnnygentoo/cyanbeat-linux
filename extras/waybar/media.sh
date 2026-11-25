#!/bin/bash
# Now-playing for waybar: CyanBeat if it's running, else cmus. Fixed 25 columns.
width=25
data=$(~/.local/bin/cyanbeat-remote --or-cmus -Q 2>/dev/null)
if [ -z "$data" ]; then
    printf '%*s\n' "$width" ''
    exit 0
fi
artist=$(grep '^tag artist' <<<"$data" | cut -d ' ' -f 3-)
title=$(grep '^tag title' <<<"$data" | cut -d ' ' -f 3-)
file=$(grep '^file' <<<"$data" | cut -d ' ' -f 2-)
status=$(grep '^status' <<<"$data" | cut -d ' ' -f 2)

if [ -z "$artist" ] && [ -z "$title" ]; then
    full_text=$(basename "$file")
elif [ -z "$artist" ]; then
    full_text="$title"
else
    full_text="$artist - $title"
fi

if [ "$status" != "playing" ]; then
    echo "         PAUSED          "
    exit 0
fi

text="$full_text   $full_text   "
step=$(( $(date +%s) * 2 % ${#text} ))
echo "${text:$step:$width}"
