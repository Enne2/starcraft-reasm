# Stato — opt-gl

- **Implementato:** diagnostica GL con prefisso `[host] GL` (creazione contesto, simboli proc mancanti, info log shader/link, mismatch readback) e retry profili GLES2 → desktop GL2.1 → GLES3 → GL3.2 core.
- **Resa:** `SC_GL_VERIFY=1` ha confrontato 307.200 pixel RGBA esatti con canvas/palette sul GLES2 hardware radeonsi, in Wayland/KWin headless.
- **Build:** C11 i686 e x86-64 PASS; link ELF32 privato PASS. Binary e log in `build/qa/opt-gl/`.
- **A/B:** due run ciascuno GL/GPU/CPU in menu sullo stesso compositor; mediana CPU 77,28% / 77,43% / 78,57%, FPS 133,75 / 131,75 / 130,5. Boot Camp non provato per il divieto di simulare input. Dettagli in `reports/REPORT-opt-gl.md`.
- **Limiti aperti:** profili di retry diversi da GLES2 non testati a runtime; Wayland desktop reale non riprodotto; screenshot A/B assente. Nessun input iniettato.
- **Modifiche sorgente proprie:** solo blocco GL/present di `runtime/shim-port/elf/host_elf.c`; i cambiamenti concorrenti alle altre sezioni non sono stati ripristinati né modificati intenzionalmente.
- **Decisioni autonome:** mantenuto fallback GL→GPU→CPU e rinviata la prova Boot Camp anziché generare input.
