#!/usr/bin/env bash
set -e
echo "This scripts is not fully runable and will return som error"
echo "please run at you own RISK and always read what this scripts do with you machin"
read -rp "Did you want to continue? (y/N): " ans

case "$ans" in
  y|Y)
    echo "=> Be careful!"
    ;;
  *)
    exit 1
    ;;
esac

sudo pacman -Syu

echo "==> Installing/Updating yay..."
git clone https://aur.archlinux.org/yay.git
cd yay
makepkg -si
cd ..
rm -rf yay

echo "==> Installing required packages..."
sudo pacman -S --needed --noconfirm \
    imlib2 dash kitty starship zsh exa \
    kitty rofi flameshot nemo zig libc++ pam libxcb xcb-util picom \
    base-devel xorgproto libx11 libxext libxrandr libxinerama libxrender libxft \
    libxfixes libxdamage libxcomposite libxmu libxtst p7zip
yay -S --needed --noconfirm zen-browser-bin

echo "==> Installing cursor..."
yay -S --noconfirm --needed bibata-cursor-theme-bin

echo "==> Installing greenclip..."
yay -S --noconfirm --needed rofi-greenclip

sudo cp -r AnDWM "$HOME"/.config/

echo "==> Installing fonts..."
sudo pacman -S --needed ttf-iosevka-nerd noto-fonts noto-fonts-cjk noto-fonts-extra ttf-hack-nerd
yay -S --noconfirm --needed ttf-iosevka
mkdir -p ~/.local/share/fonts/
cd ~/.local/share/fonts/
sudo wget https://github.com/be5invis/Sarasa-Gothic/releases/download/v1.0.35/Sarasa-SuperTTC-1.0.35.7z
sudo 7z x Sarasa-SuperTTC-1.0.35.7z
rm Sarasa-SuperTTC-1.0.35.7z
fc-cache -fv

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
    sudo systemctl disable sddm.service || true
    sudo systemctl disable lightdm.service || true
    sudo systemctl disable gdm.service || true
    sudo systemctl disable lxdm.service || true
    

    echo "=> Enabling LY..."
    sudo systemctl enable ly@tty2.service
else
    echo "=> Skipping LY install step."
fi

echo "==> Copying dotfiles..."
cd "$HOME"/AnDWM/AnDWM/
sudo cp -r AnDWM "$HOME"/.config/
sudo cp -r .config "$HOME"
sudo cp -r .icons "$HOME"
sudo cp usr/sbin/* /usr/sbin/
sudo cp -r usr/share/* /usr/share
sudo cp .Xresources "$HOME"

echo "==> Building QOL Packages..."
cd "$HOME"/.config/AnDWM/scripts/
g++ -Ofast -march=native cpp/claim-clip.cpp -o claim-clip -lX11 -lXfixes
g++ -Ofast -march=native cpp/bar.cpp -o bar

echo "==> Building QOL Packages..."
cd "$HOME"/.config/AnDWM/scripts/
g++ -Ofast -march=native cpp/claim-clip.cpp -o claim-clip -lX11 -lXfixes
g++ -Ofast -march=native cpp/bar.cpp -o bar

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

