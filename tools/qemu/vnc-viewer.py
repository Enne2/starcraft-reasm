#!/usr/bin/env python3
"""Viewer VNC minimale (GTK3 + GtkVnc, nessuna installazione richiesta).

Uso: vnc-viewer.py [host] [porta] [titolo]

Perche' esiste: su questa macchina il client VNC (krdc/remmina/tigervnc) non e'
installato, ma la libreria gtk-vnc2 si'. Cosi' la VM del Raspberry Pi Zero si
guarda in una finestra GTK nativa Wayland, indipendente dal problema della
finestra SDL di QEMU (vedi work/qemu/REPORT.md).
"""
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("GtkVnc", "2.0")
from gi.repository import Gtk, GtkVnc, GLib  # noqa: E402


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = sys.argv[2] if len(sys.argv) > 2 else "5901"
    title = sys.argv[3] if len(sys.argv) > 3 else f"VNC {host}:{port}"

    display = GtkVnc.Display()
    display.set_shared_flag(True)
    display.set_pointer_grab(True)
    # La tastiera remota manda i tasti al guest solo con "grab" esplicito:
    # lasciarla disattivata evita di catturare l'input dell'host per errore.
    display.set_keyboard_grab(False)

    def on_connected(_d):
        print("connesso", flush=True)

    def on_disconnected(_d):
        print("disconnesso", flush=True)

    def on_error(_d, message):
        print(f"errore VNC: {message}", flush=True)

    display.connect("vnc-connected", on_connected)
    display.connect("vnc-disconnected", on_disconnected)
    display.connect("vnc-error", on_error)

    window = Gtk.Window(title=title)
    window.set_default_size(800, 480)
    window.add(display)
    window.connect("destroy", Gtk.main_quit)
    window.show_all()

    display.open_host(host, port)
    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
