#!/bin/bash

#!/bin/bash

if pgrep -f "highlight-pointer -r 7 --show-cursor" > /dev/null; then
    pkill -f "highlight-pointer"
else
    ~/.config/AnDWM/scripts/bin/highlight-pointer -r 7 --show-cursor &
fi

