#!/bin/sh

app="$1"

#load colors
black=#1E1D2D
green=#a6e3a1
white=#D9E0EE
grey=#282737
blue=#96CDFB
red=#fe640b
darkblue=#83bae8
teal=#a6da95
green2=#a6da95
teal2=#5BA643
green3=#B8E9B4

# Configuration
ARTIST=$(playerctl -p "$app" metadata --format "{{ artist }}" | tr '[:lower:]' '[:upper:]' | sed 's/ - TOPIC$//')
TITLE=$(playerctl -p "$app" metadata --format "{{ title }}")
if [ -n "$ARTIST" ]; then
    TEXT="[${ARTIST}]  ${TITLE}"
else
    TEXT="${TITLE}"
fi


SCROLL_WIDTH=30
STATE_FILE="/dev/shm/scroll_pos.txt" # File to store the current scroll index (i)

# --- Main Logic ---

# 1. Initialize State File (if it doesn't exist)
if [ ! -f "$STATE_FILE" ]; then
    echo "0" > "$STATE_FILE"
fi

# 2. Read the current position (i)
i=$(cat "$STATE_FILE")

# 3. Prepare the looped text and get length using expr
SCROLL_TEXT="$TEXT  $TEXT"
TEXT_LEN=$(expr length "$TEXT")

# 4. Check if the current position exceeds the text length and reset
if [ "$i" -ge "$TEXT_LEN" ]; then
    i=0
fi

# 5. Calculate 1-based starting position for 'expr'
if [ "$TEXT_LEN" -le "$SCROLL_WIDTH" ]; then
    START_POS="1"
    SCROLL_WIDTH="$TEXT_LEN"
else
    START_POS=$((i + 1))
fi


pos=$(playerctl -p "$app" position)
len=$(playerctl -p "$app" metadata mpris:length)
len=$(expr "$len" / 1000000)
progress=$(awk -v p="$pos" -v l="$len" -v s="$SCROLL_WIDTH" 'BEGIN {printf "%d", (p / l) * s}')

# 6. Extract the visible slice using POSIX 'expr'
# Syntax: expr substr STRING START_POS LENGTH


SLICE_BEFORE=$(expr substr "$SCROLL_TEXT" "$START_POS" "$progress")
SLICE_AFTER=$(expr substr "$SCROLL_TEXT" $((START_POS + progress)) $((SCROLL_WIDTH - progress)))

# 7. Print the slice
printf "^c$black^ ^b$green2^ 󰎆 "
printf "^c$black^$SLICE_BEFORE^b$green3^$SLICE_AFTER "
printf "^d^%s""^c$blue^"
 
# 8. Update the position for the next run
NEXT_I=$((i + 1))
echo "$NEXT_I" > "$STATE_FILE"
