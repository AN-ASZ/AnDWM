#!/bin/sh
export XDG_SESSION_TYPE=x11
export DESKTOP_SESSION=dwm
export XDG_CURRENT_DESKTOP=dwm

# Ensure systemd user services (like portal) get DISPLAY/XAUTHORITY
dbus-update-activation-environment --systemd DISPLAY XAUTHORITY

xrdb merge ~/.Xresources
#xbacklight -set 10 &

#config your display here
if xrandr | grep -q "HDMI-A-0 connected"; then
    xrandr --output HDMI-A-0 --mode 1920x1080 --rate 100 --rotate inverted
    xset s off
    xset s noblank
    xset -dpms
fi

feh --bg-fill ~/.config/AnDWM/Wallpaper/Miku_plant.png
xset r rate 200 20 &

sudo ~/.config/AnDWM/scripts/bin/isolate &

wired &
~/.config/AnDWM/scripts/bin/bar &
picom --config ~/.config/AnDWM/scripts/picom.conf &

greenclip daemon &
setxkbmap -layout us,th -option grp:win_space_toggle &

/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &

# XDG autostart (Steam checkbox, etc.)
dex -a -s ~/.config/autostart >/dev/null 2>&1 &

~/.config/AnDWM/scripts/bin/bat &

#systemctl --user start opentabletdriver &

# Start DWM inside correct dbus session
exec AnDWM