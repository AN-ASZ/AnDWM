#!/bin/dash

# ^c$var^ = fg color
# ^b$var^ = bg color

interval=0

# load colors
. ~/.config/AnDWM/scripts/bar_themes/catppuccin



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
    icon="󰂄 "
  elif [ "$capacity" -ge 90 ]; then
    icon="󰁹 "
  elif [ "$capacity" -ge 80 ]; then
    icon="󰂂 "
  elif [ "$capacity" -ge 70 ]; then
    icon="󰂁 "
  elif [ "$capacity" -ge 60 ]; then
    icon="󰂀 "
  elif [ "$capacity" -ge 50 ]; then
    icon="󰁿 "
  elif [ "$capacity" -ge 40 ]; then
    icon="󰁾 "
  elif [ "$capacity" -ge 30 ]; then
    icon="󰁽 "
  elif [ "$capacity" -ge 20 ]; then
    icon="󰁼 "
  elif [ "$capacity" -ge 10 ]; then
    icon="󰁻 "
  else
    icon="󰁺 "
  fi

  printf "^c$blue^%s%s%% \n" "$icon" "$capacity"
}



#brightness() {
#  printf "^c$red^   "
#  printf "^c$red^%.0f\n" $(cat /sys/class/backlight/*/brightness)
#}






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

get_playing_player() {
    for player in $(playerctl -l >/dev/null); do
        if [ "$(playerctl -p "$player" status 2>/dev/null)" = "Playing" ]; then
            play_app="$player"
            return
        fi
    done
}

info_play() {
    mem_val=$(free -h | awk '/^Mem/ {used=$3; gsub(/i/,"",used); printf "%4.1f", used}')
    mem_unit=$(free -h | awk '/^Mem/ {print $3}' | sed 's/[0-9.]*//g')
    mem_percent=$(free -m | awk '/^Mem/ {printf("%d", $3*100/$2)}')

    if [ "$mem_percent" -lt 40 ]; then
        icon="󰾆"
        t_color=$black
    elif [ "$mem_percent" -lt 80 ]; then
        icon="󰾅"
        t_color=$black
    else
        icon="󰓅"
        t_color=$red
    fi

    interface=$(iw dev | awk '$1=="Interface"{print $2; exit}')
    strength=$(iw dev "$interface" link | awk '/signal/ {print int($2)}')

    if [ "$strength" -gt -55 ]; then
      wicon="󰤨"
    elif [ "$strength" -gt -85 ]; then
      wicon="󰤥"
    elif [ "$strength" -gt -90 ]; then
      wicon="󰤢"
    elif [ "$strength" -lt -90 ]; then
      wicon="󰤟"
    else
      wicon="󰤭"
    fi

    state=$(cat /sys/class/net/"$interface"/operstate 2>/dev/null)

    playing_found=0

    for player in $(playerctl -l 2>/dev/null); do
        status=$(playerctl -p "$player" status 2>/dev/null)
        if [ "$status" = "Playing" ]; then
            /home/hi/.config/AnDWM/scripts/player "$player"
            playing_found=1
            printf "^c$t_color^ ^b$blue^ $icon ^d^%s"
            case "$state" in
            up) printf "^c$black^ ^b$blue^ $wicon ^d^%s";;
            down) printf "^c$black^ ^b$blue^ 󰤭^d^%s";;
            esac
            break
        fi
    done

    if [ "$playing_found" -eq 0 ]; then
        echo "0" > "/dev/shm/scroll_pos.txt"
        
        cpu() {
          cpu_val=$(top -bn1 | awk '/Cpu\(s\)/ {usage = 100 - $8; printf "%4.1f", usage}')
          printf "^c$black^ ^b$green^ "
          printf "^c$white^ ^b$grey^ %s%% ^d^" "$cpu_val"
        }
        cpu
        
        printf "^c$t_color^ ^b$green^ $icon"
        printf "^c$white^ ^b$grey^ %s%s ^d^" "$mem_val" "$mem_unit"
        
        
        get_wifi="$(iwgetid -r)"
        case "$state" in
        up) printf "^c%s^ ^b%s^ $wicon^c%s^ ^b%s^ %s ^d^" "$black" "$blue" "$white" "$grey" "$get_wifi";;
        down) printf "^c%s^ ^b%s^ 󰤭^c%s^ ^b%s^ %s ^d^" "$black" "$blue" "$white" "$grey" "Disconnected";;
        esac
        
    fi
}

while true; do

  [ $interval = 0 ] || [ $(($interval % 3600)) = 0 ] &&
  interval=$((interval + 1))

  sleep 1 && xsetroot -name "   $(battery)$(info_play)$(clock)$(kb_layout)^b$black^ "
done
