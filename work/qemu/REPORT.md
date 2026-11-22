# Report QEMU Raspberry Pi OS

## Esito e milestone verificate

- **Immagine ufficiale:** download già in corso al momento dell'ispezione; non duplicato né alterato. SHA-256 verificato dal processo originale: `3ec4a2977759d5ee02926228a15c943ca79541979b2377b0a9bd5d66a9dcc0ba`. Immagine decompressa in `build/qemu/2026-04-13-raspios-bookworm-armhf.img` (5.48 GB), immagine originale lasciata intatta.
- **Boot files:** estratti dal boot FAT della medesima immagine in `tools/qemu/`: kernel ARMv6 `kernel.img` (6.12.75+rpt-rpi-v6), DTB `bcm2708-rpi-zero.dtb`, copie delle config originali per riferimento.
- **Launch riproducibile:** `tools/qemu/run.sh`, QEMU 9.2.2, `-M raspi0`, 512 MiB, kernel/DTB corrispondenti e serial/QMP solo su file/socket Unix locali. Overlay qcow2 scrivibile `build/qemu/runtime/guest.qcow2` con backing raw originale; l'immagine sorgente resta read-only per QEMU.
- **Smoke boot:** inizialmente la console seriale mostrava crash del driver `bcm2835_power_probe`. Corretto il comando kernel con `initcall_blacklist=bcm2835_pm_driver_init` (workaround documentato anche dal profilo Yocto raspi0) e `dwc_otg.fiq_fsm_enable=0`. Nuovo log dimostra `VFS: Mounted root (ext4 filesystem)` e avvio `graphical.target`/`lightdm.service`.
- **QMP/screenshot:** endpoint `build/qemu/runtime/qmp.sock` risponde a `qmp_capabilities` e `screendump`; screenshot raw 800×480 in `build/qemu/runtime/screen.ppm`.

## Limiti / stato non completato

La GUI non è ancora confermata: a circa due minuti dal boot `lightdm.service` risulta ancora in avvio e non compare una sessione desktop stabile; la schermata catturata è archiviata nel percorso sopra. Non è stato modificato il guest perché manca una sessione operativa e l'immagine base deve restare intatta. Non ho installato software host, usato sudo, rete, desktop host o git.

Di conseguenza, **guest autostart configurabile per un futuro eseguibile ARM non implementato/verificato**, e smoke GUI non superato. Il boot Linux e l'acquisizione QMP sì.

## File aggiunti / evidenza

- `tools/qemu/run.sh`
- `tools/qemu/kernel.img`, `tools/qemu/bcm2708-rpi-zero.dtb`, `tools/qemu/cmdline.original.txt`, `tools/qemu/config.original.txt`
- `build/qemu/runtime/guest.qcow2`, `serial.log`, `qmp.sock`, `screen.ppm`
- `build/qemu/launch.log`

## Ricerca tecnica

QEMU ARM Raspberry Pi documentation: https://www.qemu.org/docs/master/system/arm/raspi.html — `raspi0` è ARM1176JZF-S, 512 MiB, include seriali e framebuffer. Fonte workaround raspi0: https://git.yoctoproject.org/meta-raspberrypi/plain/conf/machine/raspberrypi0.conf (blacklist `bcm2835_pm_driver_init` e `dwc_otg.fiq_fsm_enable=0`). La ricerca ha evidenziato anche l'ordine UART differente nei setup basati su kernel diretto; in questo run `ttyAMA0` ha dato log di kernel e systemd.

## Decisioni autonome

Scelta conservativa: preservare il disco originale con overlay qcow2, usare QMP su socket Unix non esposto e non forzare modifiche alla configurazione del guest mentre LightDM non è pronto. Ho lasciato QEMU in esecuzione per consentire di esaminare seriale e screenshot. Per completare servono diagnosi del blocco LightDM/GUI e, solo dopo accesso guest affidabile, installazione/configurazione dell'autostart applicativo.
