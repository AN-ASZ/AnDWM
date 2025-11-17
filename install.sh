#!/usr/bin/env bash
set -e

echo "==> Installing fonts..."
sudo pacman -S --noconfirm --needed \
    noto-fonts \
    noto-fonts-extra \
    noto-fonts-cjk \
    ttf-nerd-fonts-symbols \
    ttf-nerd-fonts-symbols-common \
    ttf-jetbrains-mono-nerd

yay -S --noconfirm --needed ttf-noto-nerd
yay -S --noconfirm --needed ttf-iosevka

echo "==> Installing required packages..."
sudo pacman -S --needed --noconfirm \
    imlib2 dash kitty starship zsh exa \
    rofi flameshot nemo zig libc++ pam libxcb xcb-util picom \

echo "==> Installing cursor..."
yay -S --noconfirm --needed bibata-cursor-theme-bin

echo "==> Installing greenclip..."
yay -S --noconfirm --needed rofi-greenclip
 

# -------------------------
# ASK BEFORE ENABLING LY
# -------------------------
read -rp "Do you want to install Ly and disable other display manager? (y/n): " ans
if [[ "$ans" == "y" || "$ans" == "Y" ]]; then
    cd "$HOME"/.config/AnDWM/
    sudo git clone https://github.com/fairyglade/ly.git
    cd ly
    sudo zig build installexe -Dinit_system=systemd
    cd ..

    echo "=> Disabling other display managers..."
    sudo systemctl disable sddm.service lightdm.service gdm.service lxdm.service 2>/dev/null || true

    echo "=> Enabling LY..."
    sudo systemctl enable ly.service
else
    echo "=> Skipping LY install step."
fi

echo "==> Copying dotfiles..."
sudo cp -r AnDWM "$HOME"/.config/
sudo cp -r .config "$HOME"
sudo cp -r .icons "$HOME"
sudo cp usr/sbin/* /usr/sbin/
sudo cp -r usr/share/* /usr/share
sudo cp .Xresources "$HOME"

echo "==> Building and installing AnDWM..."
cd "$HOME/.config/AnDWM/AnDWM/"
sudo make install

echo "==> Creating XSession entry..."
DESKTOP_FILE="/usr/share/xsessions/AnDWM.desktop"

sudo bash -c "cat > $DESKTOP_FILE" <<EOF
[Desktop Entry]
Name=AnDWM
Comment=fork of chadwm makt it modern
Exec=$HOME/.config/AnDWM/scripts/run.sh
Type=Application
EOF

echo "==> Installation complete!"
echo "Reboot and select 'AnDWM' on login."
echo "Thank you for chadwm"