#!/usr/bin/env bash
set -e

echo "==> Installing fonts..."
sudo pacman -S --noconfirm --needed \
    ttf-iosevka \
    noto-fonts \
    noto-fonts-extra \
    noto-fonts-cjk \
    ttf-nerd-fonts-symbols \
    ttf-nerd-fonts-symbols-common \
    ttf-jetbrains-mono-nerd

yay -S --noconfirm ttf-noto-nerd

echo "==> Installing required packages..."
sudo pacman -S --needed --noconfirm \
    imlib2 dash kitty starship zsh exa rofi flameshot nemo greenclip

echo "==> Installing LY display manager..."
yay -S --noconfirm --needed ly

# -------------------------
# ASK BEFORE ENABLING LY
# -------------------------
read -rp "Enable LY and disable other display managers? (y/n): " ans
if [[ "$ans" == "y" || "$ans" == "Y" ]]; then
    echo "=> Disabling other display managers..."
    sudo systemctl disable sddm.service lightdm.service gdm.service lxdm.service 2>/dev/null || true

    echo "=> Enabling LY..."
    sudo systemctl enable ly.service
else
    echo "=> Skipping LY enable/disable step."
fi

echo "==> Copying dotfiles..."
sudo cp -r .config "$HOME"/.config
sudo cp -r .icons "$HOME"/.icons
sudo cp -r usr/ /usr/

echo "==> Building and installing AnDWM..."
cd "$HOME/.config/AnDWM"
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
