# AnDWM 

**AnDWM** is a modern, heavily customized fork of **dwm** (Dynamic Window Manager), inspired(copy) by **ChaDWM** and built for speed, aesthetics, and full keyboard control.

It follows the suckless philosophy but adds many quality-of-life features, custom utilities, and sane defaults for a modern Linux desktop.

---

## Features

* Dynamic tiling window manager (X11)
* Many layouts (spiral, grid, monocle, centered master, floating, etc.)
* Advanced gaps control (inner / outer / horizontal / vertical)
* Tab bar with previews
* Systray support
* Media keys (volume & brightness)
* Multi-monitor aware
* US / TH keyboard layout toggle
* Custom bar, battery monitor, and process isolation tools
* Catppuccin theme included
* Lightweight & fast

---

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
* `pipewire-pulse`
* `eww`
* `libnotify`
* `glib2`

**Arch Linux example:**

```bash
sudo pacman -S --needed --noconfirm \
    imlib2 dash kitty starship exa \
    kitty rofi flameshot nemo zig libc++ pam libxcb xcb-util picom \
    base-devel xorgproto libx11 libxext libxrandr libxinerama libxrender libxft \
    libxfixes libxdamage libxcomposite libxmu libxtst p7zip feh polkit-gnome \
    wireless_tools xorg-xsetroot wget xorg-server xorg-xinit xorg-xrandr xorg-xset xterm iw \
    fish git nano fastfetch less dex playerctl
```

---

# Installation

## Method 1 — Install Script (Recommended)

```bash
git clone https://github.com/AN-ASZ/AnDWM
cd AnDWM
chmod +x install.sh
./install.sh
```
Tesed :

* ``Arch Linux``

Testing :
* ``Debian``

## Method 2 — Manual
### *Clone git repository*
```bash
git clone https://github.com/AN-ASZ/AnDWM
cd AnDWM
```
### *Copy all dotfile*
```bash
sudo cp -r AnDWM "$HOME"/.config/
cd "$HOME/AnDWM/"
sudo cp -r .config "$HOME"
sudo cp .Xresources "$HOME"
```
### *Compile QOF bin*
```bash
cd "$HOME/.config/AnDWM/scripts/"
```
```bash
sudo g++ -Ofast -march=native cpp/bar.cpp -o bin/bar -lX11 -lXfixes
```
```bash
sudo g++ cpp/bat.cpp -o bin/bat -std=c++17 -O2 -pthread -march=native
```
```bash
sudo g++ cpp/isolate.cpp -o bin/isolated -O2 -pthread -march=native
```
### *Compile DWM*
```bash
cd "$HOME/.config/AnDWM/AnDWM/"
sudo make install
```
---

# Run AnDWM
### With startx

```shell
startx ~/.config/AnDWM/scripts/run.sh
```

### With Display Manager

* Create a desktop entry (make sure to change `user` with your user):

```shell
sudo touch /usr/share/xsessions/AnDWM.desktop  
```

```
[Desktop Entry]
Name=chadwm
Comment=dwm made beautiful 
Exec=/home/user/.config/chadwm/scripts/./run.sh 
Type=Application 
```


## Main Configuration (`config.def.h`)

DWM configuration file:

```
~/.config/AnDWM/AnDWM/config.h
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

```bash
sudo make install
```

---

## Additional Configuration Files

in my rice have custom c++ file that include 
* Isolated cpu for heavy load
* custom statusbar write in cpp for 0% cpu usage + playerctl include
* low battery notification for notebook
---

### `bar.cpp` — Custom Status Bar

**Purpose:**
A lightweight custom status bar used instead of external bars like Polybar.

**What it handles:**

* Time / date
* System info (CPU, memory, etc. depending on build)
* Integration with dwm bar
* Minimal overhead compared to script-based bars
* media player indicator with playerctl

**Configuration:**

* Edit variables and modules directly in:

  ```
  ~/.config/AnDWM/scripts/cpp/bar.cpp
  ```
* Recompile after changes:

  ```bash
  sudo g++ -Ofast -march=native cpp/bar.cpp -o bin/bar -lX11 -lXfixes
  ```

**Why C++ instead of shell scripts?**

* Faster updates
* Lower CPU usage
* No shell spawning

---

### `bat.cpp` — Battery Monitor & Notifications

**Purpose:**
Native battery monitoring with desktop notifications.

**Features:**

* Reads battery info from `/sys/class/power_supply`
* Sends notifications using `libnotify`
* Alerts when battery is critically low
* Extremely lightweight

**Configurable values inside `bat.cpp`:**

* Battery path (`BAT0`)
* Low battery threshold (default: very low %)
* Check interval

**File location:**

```
~/.config/AnDWM/scripts/cpp/bat.cpp
```

**Build dependency:**

* `libnotify`
* `glib`

---
* Recompile after changes:

```bash
sudo g++ cpp/bat.cpp -o bin/bat -std=c++17 -O2 -pthread -march=native
```

---

### `isolate.cpp` — Process CPU Isolation

**Purpose:**
Force specific processes (e.g. PipeWire, audio servers) to run on selected CPU cores.

**Why this exists:**

* Reduce audio glitches
* Improve system responsiveness
* Keep background services from stealing CPU time
* keep cpu soft isolated while SCX schedule enable

**Example use cases:**

* Isolate `pipewire`
* Isolate real-time audio/video services
* Improve gaming / low-latency workloads

**Configurable inside:**

* Target process name(s)
* CPU core mask
* Scheduling behavior

**File location:**

```
~/.config/AnDWM/scripts/cpp/isolate.cpp
```
---

Uses:
* `/proc`
* Linux scheduler APIs
* CPU affinity (`taskset` logic in native code)
---

* Recompile after changes:

```bash
sudo g++ cpp/isolate.cpp -o bin/isolated -O2 -pthread -march=native
```

## Layouts

| Symbol | Layout                   |   |                 |
| ------ | ------------------------ | - | --------------- |
| `[@]`  | Spiral (default)         |   |                 |
| `:::`  | Gapless Grid             |   |                 |
| `[\\]` | Dwindle                  |   |                 |
| `[M]`  | Monocle                  |   |                 |
| `[]=`  | Tile                     |   |                 |
| `H[]`  | Deck                     |   |                 |
| `TTT`  | Bottom Stack             |   |                 |
| `===`  | Horizontal Bottom Stack  |   |                 |
| `HHH`  | Grid                     |   |                 |
| `###`  | N-Row Grid               |   |                 |
| `---`  | Horizontal Grid          |   |                 |
| `      | M                        | ` | Centered Master |
| `>M>`  | Centered Floating Master |   |                 |
| `><>`  | Floating                 |   |                 |

---

## Keybindings

> **MOD = Super / Windows key**

### Essentials

| Key               | Action              |
| ----------------- | ------------------- |
| `MOD + Enter`     | Terminal (Kitty)    |
| `MOD + A`         | App launcher (Rofi) |
| `MOD + E`         | File manager        |
| `MOD + B`         | Browser             |
| `MOD + Q`         | Kill window         |
| `MOD + Shift + R` | Restart AnDWM       |

*(See `config.h` for the full keybinding list.)*

---

## Screenshots
```
demo/
├── desktop.png
├── tiled.png
└── floating.png
```

*Add your rice here*

---

## License

MIT License
See `LICENSE` for details.

---

## Credits

* **suckless.org** — dwm
* **ChaDWM**
* Community patches & contributors
```https://github.com/siduck/chadwm```

---

## Disclaimer

AnDWM is **not a desktop environment**.
and it just made for me only
if you want to try out so ***Keep an eye on it***

You are expected to configure:

* Compositor
* Wallpaper manager
* Autostart apps
* Status modules

This is intentional — minimal and fast.

