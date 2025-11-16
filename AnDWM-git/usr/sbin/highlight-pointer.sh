#!/bin/bash

#!/bin/bash

if pgrep -f "highlight-pointer --show-cursor" > /dev/null; then
    pkill -f "highlight-pointer --show-cursor"
else
    highlight-pointer --show-cursor &
fi

