# AnDWM (my config) - (Initial Look)
https://github.com/user-attachments/assets/a37d4c88-ccac-45ad-9f32-568b6a9bbbf5

# Requirements

* dash (shell)
* imlib2
* xsetroot package (status2d uses this to add colors to the dwm bar)
* JetBrainsMono Nerd Font or any Nerd Font (make sure to set it in `config.def.h`)
* Make sure to match your terminal theme with chadwm's theme, such as Nord, OneDark, etc.

## Other Requirements

* picom
* feh
* acpi
* rofi

# Install

```sh
git clone https://github.com/siduck/chadwm --depth 1 ~/.config/chadwm
cd ~/.config/chadwm/
mv eww ~/.config
cd chadwm
sudo make install
```

> Note: Make all scripts executable using `chmod +x`.

# Run chadwm

## With startx

```sh
startx ~/.config/chadwm/scripts/run.sh
```

## With sx

```sh
sx sh ~/.config/chadwm/scripts/run.sh
```

You can create an alias for convenience:

```sh
alias chadwm='startx ~/.config/chadwm/scripts/run.sh'
```

## With a Display Manager

Create a desktop entry (replace `user` with your username):

```sh
sudo touch /usr/share/xsessions/chadwm.desktop
```

```ini
[Desktop Entry]
Name=chadwm
Comment=Beautifully customized dwm
Exec=/home/user/.config/chadwm/scripts/run.sh
Type=Application
```

# Recompile

You need to recompile dwm after every change you make to the source code.

```sh
cd ~/.config/chadwm/chadwm
rm config.h
sudo make install
```

# Change Themes

* Bar: edit `bar.sh` (line 9) and `config.def.h` (line 35)
* Rofi: edit `config.rasi` (line 15)

# Eww

All eww-related stuff has been removed.

# Credits

* Huge thanks to [eProTaLT83](https://www.reddit.com/user/eProTaLT83). I wanted certain features in dwm like tabbed monocle mode, tag previews, etc., and he implemented my ideas and created patches for me. I can't even count how many times he has helped me :v
* @fitrh helped with the [colorful tag patch](https://github.com/fitrh/dwm/issues/1)
* [6gk](https://github.com/6gk/fet.sh) — eww's pure POSIX fetch functions were taken from here
* [mafetch](https://github.com/fikriomar16/mafetch) — a modified version was used as the fetch shown in the screenshots

# Patches

* [systray](https://gitlab.com/-/snippets/2184056)
* systray iconsize
* barpadding
* bottomstack
* cfacts
* dragmfact
* dragcfact (taken from [bakkeby's build](https://github.com/bakkeby/dwm-flexipatch))
* fibonacci
* gaplessgrid
* horizgrid
* movestack
* vanity gaps
* colorful tags
* statuspadding
* status2d
* underline tags
* notitle
* winicon
* [preserveonrestart](https://github.com/PhyTech-R0/dwm-phyOS/blob/master/patches/dwm-6.3-patches/dwm-preserveonrestart-6.3.diff) — prevents all windows from being moved to tag 1 after restarting dwm
* shiftview

# Original Repository

[https://github.com/siduck/chadwm.git](https://github.com/siduck/chadwm.git)
