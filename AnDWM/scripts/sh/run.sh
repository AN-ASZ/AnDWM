#!/bin/sh

xrdb merge ~/.Xresources 
xbacklight -set 10 &

#if xrandr | grep -q "HDMI1 connected"; then
#    xrandr --output eDP1 --off
#    xrandr --output HDMI1 --mode 1920x1080 --rate 100 --rotate inverted
#    xrandr --newmode "1280x729_100.00" 133.75 1280 1368 1504 1728 729 732 742 775 -hsync -vsync
#    xrandr --addmode HDMI1 "1280x729_100.00"
#fi

if xrandr | grep -q "HDMI-A-0 connected"; then
    xrandr --output HDMI-A-0 --mode 1920x1080 --rate 100 --rotate inverted
    xset s off
    xset s noblank
    xset -dpms
fi

feh --bg-fill ~/.config/AnDWM/Wallpaper/Miku_plant.png
xset r rate 200 50 &

wired &
dash ~/.config/AnDWM/scripts/bar.sh &
picom --config ~/.config/AnDWM/scripts/picom.conf &
greenclip daemon &
setxkbmap -layout us,th -option grp:win_space_toggle &

#easyeffects --gapplication-service &

#PolicyKit
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &

while type AnDWM >/dev/null; do AnDWM && continue || break; done
