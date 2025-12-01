#!/bin/dash

# ^c$var^ = fg color
# ^b$var^ = bg color

interval=0

# load colors
. ~/.config/chadwm/scripts/bar_themes/catppuccin

cpu() {
  cpu_val=$(awk '{sum+=$1; count++} END {if(count>0) printf("%.2f GHz\n", sum/count/1000000)}' /sys/devices/system/cpu/*/cpufreq/scaling_cur_freq)

  printf "^c$black^ ^b$green^ CPU"
  printf "^c$white^ ^b$grey^ $cpu_val "
}

#pkg_updates() {
#  #updates=$({ timeout 20 doas xbps-install -un 2>/dev/null || true; } | wc -l) # void
#  updates=$({ timeout 20 checkupdates 2>/dev/null || true; } | wc -l) # arch
#  # updates=$({ timeout 20 aptitude search '~U' 2>/dev/null || true; } | wc -l)  # apt (ubuntu, debian etc)
#  if [ -z "$updates" ]; then
#    printf "  ^c$green^    Fully Updated"
#  else
#    printf "  ^c$green^    $updates"" updates"
#  fi
#}

battery() {
  capacity=$(cat /sys/class/power_supply/BAT0/capacity)
  charging=$(cat /sys/class/power_supply/AC0/online)

  if [ "$charging" -eq 1 ]; then
    icon="󰂄"
  elif [ "$capacity" -ge 90 ]; then
    icon="󰁹"
  elif [ "$capacity" -ge 80 ]; then
    icon="󰂂"
  elif [ "$capacity" -ge 70 ]; then
    icon="󰂁"
  elif [ "$capacity" -ge 60 ]; then
    icon="󰂀"
  elif [ "$capacity" -ge 50 ]; then
    icon="󰁿"
  elif [ "$capacity" -ge 40 ]; then
    icon="󰁾"
  elif [ "$capacity" -ge 30 ]; then
    icon="󰁽"
  elif [ "$capacity" -ge 20 ]; then
    icon="󰁼"
  elif [ "$capacity" -ge 10 ]; then
    icon="󰁻"
  else
    icon="󰁺"
  fi

  printf "^c$blue^ %s %s%%\n" "$icon" "$capacity"
}



#brightness() {
#  printf "^c$red^   "
#  printf "^c$red^%.0f\n" $(cat /sys/class/backlight/*/brightness)
#}

mem() {
  printf "^c$blue^^b$black^  "
  printf "^c$blue^$(free -h | awk '/^Mem/ { print $3 }' | sed s/i//g)"
}

wlan() {
  interface=$(iw dev | awk '$1=="Interface"{print $2; exit}')
  strength=$(iw dev "$interface" link | awk '/signal/ {print int($2)}')

  if [ "$strength" -gt -55 ]; then
    wicon="󰤨"
  elif [ "$strength" -gt -85 ]; then
    wicon="󰤥"
  elif [ "$strength" -gt -90 ]; then
    wicon="󰤢"
  else
    wicon="󰤟"
  fi

  get_wifi="$(iwgetid -r)"
  state=$(cat /sys/class/net/"$interface"/operstate 2>/dev/null)

  case "$state" in
    up) printf "^c$black^ ^b$blue^ $wicon ^d^%s" " ^c$blue^$get_wifi" ;;
    down) printf "^c$black^ ^b$blue^ 󰤭 ^d^%s" " ^c$blue^Disconnected" ;;
  esac
}


clock() {
	printf "^c$black^ ^b$darkblue^ 󱑆 "
	printf "^c$black^^b$blue^ $(date '+%H:%M') "
  printf "^d^%s""^c$blue^"
}

kb_layout() {
    layout=$(xkblayout-state print "%s" | tr '[:lower:]' '[:upper:]')
    printf "^c$black^ ^b$blue^ $layout "
    printf "^d^%s""^c$blue^"
}

player() {
    if [ "$(playerctl status 2>/dev/null)" = "Playing" ]; then
        /home/hi/.config/chadwm/scripts/player.sh
    fi
}

while true; do

  [ $interval = 0 ] || [ $(($interval % 3600)) = 0 ] &&
  interval=$((interval + 1))

  sleep 1 && xsetroot -name "   $(player) $(battery)$(cpu)$(mem)$(wlan)$(clock)$(kb_layout)^b$black^ "
done
