#!/bin/sh
# minimal.sh — riduce Raspberry Pi OS (guest raspi0) a X11 + openbox + lxterminal.
# Eseguito una volta come root nel guest:  sudo sh /home/pi/minimal.sh
# Motivazione e fonti: work/qemu/REPORT.md (sezione "Desktop minimale").
set -e

# --- 1) sessione X minimale per l'utente pi -------------------------------
cat > /home/pi/.xinitrc <<'XINITRC'
#!/bin/sh
# Sessione minimale: sfondo, niente screensaver/DPMS, un terminale, openbox.
xsetroot -solid '#204080' 2>/dev/null || true
xset s off -dpms s noblank 2>/dev/null || true
lxterminal --title="StarCraft ARMv6" --geometry=100x30+0+0 &
exec openbox
XINITRC
chmod 755 /home/pi/.xinitrc
chown pi:pi /home/pi/.xinitrc

# --- 2) servizio systemd che avvia X su tty1 -----------------------------
cat > /etc/systemd/system/minimal-x.service <<'UNIT'
[Unit]
Description=Minimal X11 session (openbox + lxterminal)
After=systemd-user-sessions.service
Conflicts=getty@tty1.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi
PAMName=login
Environment=XDG_SESSION_TYPE=x11
TTYPath=/dev/tty1
StandardInput=tty
StandardOutput=journal
StandardError=journal
UnsetEnvironment=TERM
UtmpIdentifier=tty1
UtmpMode=user
ExecStartPre=+/usr/bin/chvt 1
ExecStart=/usr/bin/startx -- :0 vt1 -keeptty -nolisten tcp
Restart=no
TimeoutStopSec=5

[Install]
WantedBy=multi-user.target
UNIT
chmod 644 /etc/systemd/system/minimal-x.service

systemctl daemon-reload
systemctl disable --now lightdm.service 2>/dev/null || true
systemctl enable minimal-x.service

# --- 3) servizi inutili su un Pi Zero emulato ----------------------------
for u in cups cups-browsed avahi-daemon bluetooth ModemManager triggerhappy \
         udisks2 accounts-daemon NetworkManager wpa_supplicant \
         rpi-eeprom-update piwiz; do
    systemctl disable --now "$u" 2>/dev/null || true
    systemctl mask "$u" 2>/dev/null || true
done

systemctl set-default multi-user.target

# --- 4) residui LXDE non più usati --------------------------------------
if [ -f /etc/xdg/lxsession/LXDE-pi/autostart ]; then
    sed -i '/lxpanel\|pcmanfm\|xscreensaver\|starcraft-autostart/d' \
        /etc/xdg/lxsession/LXDE-pi/autostart
fi
rm -f /usr/local/bin/starcraft-autostart.sh

# --- 5) niente blanking della console -----------------------------------
cat > /etc/systemd/system/console-noblank.service <<'UNIT'
[Unit]
Description=Disable console blanking
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'echo 0 > /sys/module/kernel/parameters/consoleblank 2>/dev/null || true'

[Install]
WantedBy=multi-user.target
UNIT
systemctl enable console-noblank.service 2>/dev/null || true

sync
echo MINIMAL_DONE
echo MINIMAL_DONE > /dev/console 2>/dev/null || true
