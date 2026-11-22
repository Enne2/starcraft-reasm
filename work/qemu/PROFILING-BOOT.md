# Profiling del boot — Raspberry Pi OS Trixie Lite in QEMU raspi0

Data: 2026-11-20. Guest: Raspberry Pi OS Lite 32-bit 2026-10-06 (Trixie), kernel 6.18.50+rpt-rpi-v6.
Host: frigate (AMD Ryzen 7 255, Zen 4). QEMU 9.2.2 arm-softmmu, macchina `raspi0`.

## 1. La configurazione emulata è equivalente al Pi Zero?

| Voce | Pi Zero reale | QEMU `-M raspi0` | Esito |
|---|---|---|---|
| CPU | ARM1176JZF-S rev 7 | idem (`/proc/cpuinfo`: `CPU part 0xb76`, `rev 7`, `v6l`) | equivalente |
| RAM | 512 MiB | 512 MiB (`-m 512M`) | equivalente |
| Periferiche | BCM2835 (fb, USB DWC2, SD, PL011, AUX UART, RNG, GPIO) | tutte modellate (doc QEMU) | equivalente |
| Accelerazione | n/a | solo TCG (`-accel help` → `tcg`) | nessuna accel. possibile: KVM non emula ARM su host x86 |
| Frequenza | 1 GHz | **non esiste in TCG** | non confrontabile |
| BogoMIPS | 697,95 | 1869,41 | **artefatto** di calibrazione del delay loop, non è velocità |

Non ci sono impostazioni "sbagliate": la board è quella giusta. La velocità in TCG dipende
dall'host e dal carico, non da un clock emulato.

### Fattore di emulazione misurato (stessi carichi, guest vs host)

| Carico | Guest | Host | Rapporto | Pi Zero reale (riferimento) |
|---|---|---|---|---|
| SHA256 64 MB | 2,697 s (23,7 MB/s) | 0,051 s (1255 MB/s) | 53x | ~10-15 MB/s → **guest più veloce** |
| 100 × fork+exec `/bin/true` | 2,061 s (20,6 ms/exec) | 0,072 s (0,72 ms) | 29x | ~5-8 ms/exec → guest 2,5-4x più lento |
| Lettura raw `mmcblk0` 64 MB | 0,030 s (cache host) | — | — | — |

Conclusione parziale: sui carichi CPU-bound l'emulato è **più veloce** del Pi Zero vero; paga
2,5-4x soltanto su fork/exec, che è ciò che systemd e udev fanno in continuazione.

## 2. Profiling del boot (systemd-analyze)

Profilo **A** — immagine Lite appena installata (X a mano, cloud-init e NetworkManager attivi):

```
Startup finished in 6.198s (kernel) + 3min 57.494s (userspace) = 4min 3.692s
multi-user.target reached after 2min 21.448s in userspace
```

Catena critica (A):

```
multi-user.target @2min 21.448s
└─ssh.service @2min 15.615s +5.806s
  └─network.target @2min 14.313s
    └─NetworkManager.service @1min 44.774s +29.474s
      └─dbus.service @1min 41.300s
        └─basic.target @1min 40.812s
          └─sysinit.target @1min 40.208s
            └─cloud-init-network.service @1min 37.406s
              └─cloud-init-local.service @1min 32.184s
                └─cloud-init-main.service @1min 9.630s +22.498s
                  └─boot-firmware.mount @1min 8.604s +682ms
                    └─systemd-fsck@...-01.service @58.496s +9.717s
```

Top di `blame` (A): `systemd-networkd-wait-online` 124,5 s (attesa inutile: la NIC è di
NetworkManager), `dev-mmcblk0p2.device` 33,8 s, `NetworkManager` 29,5 s,
`cloud-init-main` 22,5 s, `rpi-resize-swap-file` 19,3 s, `systemd-udev-trigger` 12,6 s,
`fsck` 9,7 s; poi una coda di mount/modprobe da 7-8 s ciascuno.

Profilo **B** — cloud-init, `systemd-networkd-wait-online`, `NetworkManager-wait-online`,
`rpi-resize-swap-file`, `avahi` disabilitati:

```
Startup finished in 6.627s (kernel) + 2min 14.073s (userspace) = 2min 20.700s
multi-user.target reached after 2min 13.990s in userspace
```
→ guadagno **-7 s** soltanto: la catena critica era già passata per cloud-init e NetworkManager.

Profilo **C** — in più mascherati cloud-init (tutti), swap-resize, NetworkManager,
wpa_supplicant, systemd-networkd, ssh, avahi:

```
Startup finished in 5.913s (kernel) + 1min 43.514s (userspace)
prompt di login a 108 s dal lancio di QEMU (include ~4 s di avvio emulatore)
```
→ **-42 s** rispetto ad A, ma restano ~100 s di userspace.

Residuo del profilo C: `dev-mmcblk0p2.device` ~35 s, `udev-trigger` ~12 s, journald ~7 s,
una decina di mount/modprobe da ~7 s l'uno. Sono costi di **fork+exec + udev/syscall emulati**,
non di calcolo.

## 3. Confronto con l'hardware vero (fonti)

1. **Charles Hallard, ago 2026 — Raspberry Pi OS Trixie Lite su Pi Zero W reale**:
   `13.420s (kernel) + 2min 20.534s (userspace) = 2min 33.955s`, multi-user a `1min 41.997s`,
   stessi colpevoli (cloud-init → sysinit a 48,8 s, poi NetworkManager +49 s).
   Con cloud-init off + `auto_initramfs=0` scende a **34,8 s** (kernel 3,98 s).
   https://hallard.me/speed-up-boot-time-on-pi-zero-with-raspbian-trixie/
2. **Forum Raspberry Pi — Pi Zero W, Raspberry Pi OS Lite Legacy (Bookworm)**:
   `4.446s (kernel) + 24.813s (userspace) = 29.259s`, `dev-mmcblk0p2.device` 9,2 s.
   https://forums.raspberrypi.com/viewtopic.php?t=370091
3. **Forum Raspberry Pi — Pi Zero con Raspbian Lite + openbox**: kernel 3 s,
   userspace 16,5 s, app pronta a 35 s, `dev-mmcblk0p2.device` 5,7 s.
   https://forums.raspberrypi.com/viewtopic.php?t=305944
4. **Forum Raspberry Pi — Pi Zero 2016**: `2.377s (kernel) + 17.066s (userspace) = 19.443s`.
   https://forums.raspberrypi.com/viewtopic.php?t=137584
5. **BogoMIPS del Pi Zero reale = 697,95** (stessa discussione, risposta di berry120).
   https://raspberrypi.stackexchange.com/questions/333/
6. **Doc QEMU**: `raspi0`/`raspi1ap` = ARM1176JZF-S, 512 MiB — nessuna differenza di specifica.
   https://www.qemu.org/docs/master/system/arm/raspi.html

## 4. Conclusione

- **Non è l'emulatore**: la stessa immagine su Pi Zero **reale** impiega 2min34 s
  (multi-user 1min42 s). Il nostro profilo A (multi-user 141 s) è **1,4x più lento del ferro**,
  non 10x; il kernel emulato è anzi **2x più veloce** (6,2 s vs 13,4 s, perché QEMU passa
  `-kernel` diretto e salta i 14 MB di initramfs).
- La lentezza è **dell'immagine Lite Trixie**: cloud-init (~50 s in catena critica) e
  NetworkManager (~30-50 s) più l'attesa `systemd-networkd-wait-online` (124 s inutili,
  disattivabile).
- Il residuo dopo il profilo C (~100 s) è emulazione di udev/systemd (fork+exec 20,6 ms vs
  5-8 ms reali) e dell'MMC emulato: su ferro la stessa voce vale 5,7-9,2 s invece di ~35 s.
- Per scendere ancora: journald volatile, riduzione dei mount, e verificare se il device SD
  emulato può essere accelerato (cache/writeback, `if=sd` vs altre modalità).

## 5. Stato attuale della VM

Il disco di lavoro è nel profilo **C**: cloud-init, swap-resize, NetworkManager,
wpa_supplicant, systemd-networkd, ssh e avahi sono disabilitati/mascherati. Questo toglie
la rete (e quindi l'accesso SSH e la connettività dal guest): va ripristinato quello che serve.
