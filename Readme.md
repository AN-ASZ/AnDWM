# AnDWM 

**AnDWM** is a modern, heavily customized fork of **dwm** (Dynamic Window Manager), inspired(copy) by **ChaDWM** and built for speed, aesthetics, and full keyboard control.

---

## Demo


https://github.com/user-attachments/assets/d47fd978-35b7-4f38-9909-73d7a0f3478c

### Todo list :-
* add more automation compile in MAKEFILE
* recompose readme.md file

## Requirements

* `aur(yay)`
* `Xorg (X11)`
* `gcc`, `g++`, `make`, `sudo`
* `libX11`, `libXft`, `libXinerama`
* `rofi`
* `kitty`
* `nemo`
* `maim`
* `xclip`
* `light`
* `libnotify`
* `glib2`
* `ttf-iosevka-nerd`
* `bibata-cursor-theme-bin`

see in install scripts for full optional package

---

**Arch Linux example:**

*install yay :*
```bash
sudo pacman -S --needed --noconfirm git base-devel
tmpdir=$(mktemp -d)

git clone https://aur.archlinux.org/yay.git "$tmpdir/yay"
(cd "$tmpdir/yay" && makepkg -si --noconfirm)

sudo rm -rf "$tmpdir"
```

*via pacman :*
```bash
sudo pacman -S --needed --noconfirm  imlib2 dash kitty starship exa kitty rofi flameshot nemo zig libc++ pam libxcb xcb-util picom base-devel xorgproto libx11 libxext libxrandr libxinerama libxrender libxft libxfixes libxdamage libxcomposite libxmu libxtst p7zip feh polkit-gnome wireless_tools xorg-xsetroot wget xorg-server xorg-xinit xorg-xrandr xorg-xset xterm iw fish git nano fastfetch less dex playerctl ttf-iosevka-nerd noto-fonts noto-fonts-cjk noto-fonts-extra ttf-hack-nerd
```

*via yay :*
```bash
yay -S --needed --noconfirm zen-browser-bin xkblayout-state-git bibata-cursor-theme-bin rofi-greenclip ttf-iosevka
```

---

# Installation

## Method 1 — Install Script (Not Recommend)
- hope this will work

```bash
git clone https://github.com/AN-ASZ/AnDWM
cd AnDWM
chmod +x install.sh
./install.sh
```
Tesed :

* ``Arch Linux``

**DONT** :
* ``Debian``
* ``Other Random Distro``
* ``Not Arch Base``

## Method 2 — Manual
### 1. Clone the Repository
Clone the repository somewhere what you want:

```bash
git clone https://github.com/an-asz/AnDWM.git
cd AnDWM
```
---

### 2. Copy Main Config Directory

Copy the main AnDWM config folder into `~/.config`:

```bash
cp -r AnDWM ~/.config/
```

Ensure correct permissions:

```bash
chmod -R a+rwX ~/.config/AnDWM
chown -R $USER:$USER ~/.config/AnDWM
```

---

### 3. Copy User Dotfiles

Copy `.config`

```bash
cp -r .config ~/
```

Copy icons

```bash
cp -r .icons ~/
```

Ensure correct permissions:

```bash
chmod -R a+rwX ~/.config ~/.icons
chown -R $USER:$USER ~/.config ~/.icons
```

---

### 4. Copy System-Wide Themes/Icons Files

require `sudo`

```bash
sudo cp -r usr/share/* /usr/share/
```

Copy Xresources

```bash
cp .Xresources ~/
```

---

### 5. Fonts (Manual)

If fonts are already installed, you may skip this step.

If fonts are included in the repository:

```bash
mkdir -p ~/.local/share/fonts
cp -r fonts/* ~/.local/share/fonts/
fc-cache -fv
```

---

### 6. Build Internal Utilities (QOL Tools)

```bash
cd ~/.config/AnDWM/scripts/
```

Compile :

```bash
g++ -Ofast -march=native cpp/bar.cpp -o bin/bar -lX11 -lXfixes
g++ cpp/bat.cpp -o bin/bat -std=c++17 -O2 -pthread -march=native
g++ cpp/isolate.cpp -o bin/isolated -O2 -pthread -march=native
```

---

### 7. Build and Install AnDWM

```bash
cd ~/.config/AnDWM/AnDWM
sudo make install
```
---

## Included Patch

- [systray](https://gitlab.com/-/snippets/2184056)
- systray iconsize
- barpadding 
- bottomstack
- cfacts
- dragmfact 
- dragcfact (took from [bakkeby's build](https://github.com/bakkeby/dwm-flexipatch))
- fibonacci
- gaplessgrid
- horizgrid
- movestack 
- vanity gaps
- colorful tags
- statuspadding 
- status2d
- underline tags
- notitle
- winicon
- [preserveonrestart](https://github.com/PhyTech-R0/dwm-phyOS/blob/master/patches/dwm-6.3-patches/dwm-preserveonrestart-6.3.diff). This patch doesnt let all windows mix up into tag 1 after restarting dwm.
- shiftview

---

# Run AnDWM
### With startx

```shell
startx ~/.config/AnDWM/scripts/run.sh
```

### With Display Manager

Create the desktop entry :

```bash
sudo nano /usr/share/xsessions/AnDWM.desktop
```

Paste the following:

```ini
[Desktop Entry]
Name=AnDWM
Comment=DWM
Exec=$HOME/.config/AnDWM/scripts/sh/run.sh
Type=Application
```
Save and exit.

# Main Configuration (`config.def.h`)

DWM configuration file:

```
~/.config/AnDWM/AnDWM/config.def.h
```

Controls:

* Keybindings
* Layouts
* Gaps
* Themes & colors
* Fonts (multi-language support)
* Window rules
* Bar behavior

After editing:
- You need to recompile dwm after every change you make to its source code.

```
cd ~/.config/AnDWM/AnDWM/
rm config.h
sudo make install
```

---

# Additional Configuration (`bar.cpp`)
BAR configuration file:

```
~/.config/AnDWM/scripts/cpp/bar.cpp
```

Feature:

* Battery
* CPU/RAM usage
* Playing media name (While playing only)
* WiFi name
* Time
* US/TH kb layout

After editing:
- You need to recompile after every change you make to its source code.

```
cd ~/.config/AnDWM/scripts
sudo g++ -O2 -march=native /cpp/bar.cpp -o /bin/bar -lX11 -lXfixes
```
---

## Layouts

| Symbol | Layout                   |
| ------ | ------------------------ |
| `[@]`  | Spiral (default)         |
| `:::`  | Gapless Grid             |
| `[\\]` | Dwindle                  |
| `[M]`  | Monocle                  |
| `[]=`  | Tile                     |
| `H[]`  | Deck                     |
| `TTT`  | Bottom Stack             |
| `===`  | Horizontal Bottom Stack  |
| `HHH`  | Grid                     |
| `###`  | N-Row Grid               |
| `---`  | Horizontal Grid          |
| `      | M                        |
| `>M>`  | Centered Floating Master |
| `><>`  | Floating                 |

---

# Keybindings

> This configuration uses **Super** (Windows Key) as the primary `MODKEY`.

## System & Applications

| Key | Action |
| --- | --- |
| `MOD + Enter` | Open Terminal (**Kitty**) |
| `MOD + A` | Open App Launcher (**Rofi**) |
| `MOD + E` | Open File Manager (**Nemo**) |
| `MOD + B` | Open Browser (**Zen Browser**) |
| `MOD + V` | Clipboard Manager |
| `MOD + .` | Emoji Picker |
| `MOD + Shift + S` | Screenshot (Select Area) |
| `MOD + Space` | Toggle Keyboard Layout (US/TH) |

## Window Management

| Key | Action |
| --- | --- |
| `MOD + Q` | Close Focused Window |
| `MOD + Shift + Q` | Force Kill Window |
| `MOD + J` / `K` | Focus Next / Previous Window |
| `MOD + Shift + J` / `K` | Move Window Position |
| `MOD + W` | Toggle Floating Mode |
| `MOD + F` | Toggle Fullscreen |
| `MOD + H` | Hide (Minimize) Window |
| `MOD + Shift + H` | Restore (Unminimize) Window |
| `MOD + Shift + B` | Make focus app into background |

## Layout & Workspace

| Key | Action |
| --- | --- |
| `MOD + T` | Spiral Layout (Default) |
| `MOD + M` | Dwindle Layout |
| `MOD + Shift + F` | Gapless Grid Layout |
| `MOD + Ctrl + G` | Horizontal Grid Layout |
| `MOD + [1-9]` | Switch Workspace |
| `MOD + Shift + [1-9]` | Move Window to Workspace |
| `MOD + Left` / `Right` | Cycle Through Workspaces |

## Controls & Gaps

| Key | Action |
| --- | --- |
| `MOD + Shift + R` | **Restart AnDWM** |
| `MOD + Ctrl + B` | Show/Hide Status Bar |
| `MOD + Ctrl + T` | Toggle Gaps On/Off |
| `MOD + Ctrl + I` / `D` | Increase/Decrease All Gaps |
| `Fn + Vol Up/Down` | System Volume |
| `Fn + Brightness` | Screen Brightness |

---
## Note
AnDWM is **not a desktop environment**.
and it just made to work for me only
if you want to try out so ***Keep an eye on it***
this project probably not working for you so feel free to blame my code


## Credits

* **suckless.org** — dwm
* **ChaDWM** — ```https://github.com/siduck/chadwm```
