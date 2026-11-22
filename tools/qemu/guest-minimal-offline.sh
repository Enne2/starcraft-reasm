#!/bin/sh
# minimal-offline.sh — riduce il guest raspi0 a X11 + openbox + lxterminal.
# Va eseguito con init=/bin/sh (systemd NON e' PID 1): usa solo symlink, mai systemctl.
set -e

SYS=/etc/systemd/system
LIB=/lib/systemd/system

mask_unit() {           # mask: /dev/null al posto dell'unita'
    ln -sf /dev/null "$SYS/$1"
}
disable_unit() {        # rimuove i .wants che la abilitano
    for d in "$SYS"/*.wants "$SYS"/*.requires; do
        [ -d "$d" ] || continue
        rm -f "$d/$1"
    done
}

# --- 1) .xinitrc dell'utente pi -------------------------------------------
cat > /home/pi/.xinitrc <<'XINITRC'
#!/bin/sh
xsetroot -solid '#204080' 2>/dev/null || true
xset s off -dpms s noblank 2>/dev/null || true
lxterminal --title="StarCraft ARMv6" --geometry=100x30+0+0 &
exec openbox
XINITRC
chmod 755 /home/pi/.xinitrc
chown 1000:1000 /home/pi/.xinitrc

# --- 2) sessione X minima su tty1 ----------------------------------------
cat > "$SYS/minimal-x.service" <<'UNIT'
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
chmod 644 "$SYS/minimal-x.service"

# --- 3) stop al desktop pesante e ai servizi inutili --------------------
disable_unit lightdm.service
mask_unit lightdm.service

for u in cups.service cups-browsed.service avahi-daemon.service \
         bluetooth.service ModemManager.service triggerhappy.service \
         udisks2.service accounts-daemon.service NetworkManager.service \
         wpa_supplicant.service rpi-eeprom-update.service piwiz.service \
         NetworkManager-wait-online.service plymouth-quit-wait.service \
         plymouth-start.service userconfig.service glamor-test.service \
         rp1-test.service; do
    disable_unit "$u"
    mask_unit "$u"
done

# --- 4) abilita la sessione minima e il target testuale -----------------
mkdir -p "$SYS/multi-user.target.wants"
ln -sf "$SYS/minimal-x.service" "$SYS/multi-user.target.wants/minimal-x.service"
ln -sf "$LIB/multi-user.target" "$SYS/default.target"

# getty su tty1 non serve: lo occupa X
rm -f "$SYS/getty.target.wants/getty@tty1.service" 2>/dev/null || true
ln -sf /dev/null "$SYS/getty@tty1.service"

# --- 5) via i residui LXDE ---------------------------------------------
if [ -f /etc/xdg/lxsession/LXDE-pi/autostart ]; then
    grep -v -E 'lxpanel|pcmanfm|xscreensaver|starcraft-autostart' \
        /etc/xdg/lxsession/LXDE-pi/autostart > /tmp/a.$$ || true
    mv /tmp/a.$$ /etc/xdg/lxsession/LXDE-pi/autostart
fi
rm -f /usr/local/bin/starcraft-autostart.sh

sync
echo MINIMAL_DONE
