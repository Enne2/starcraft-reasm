# Preludio obbligatorio
- Oracolo primario: sorgenti in `~/dev/starcraft-reasm-smacker/`.
- **Proprietà esclusiva di questo agente (NON toccare file altrui):**
  - `runtime/shim-port/elf/host_elf.c`
  - `tests/x86_gl_palette_test/`
  - `tools/build.sh`, `tools/build-runtime-x86.sh`, `tools/build-shim-x86.sh`, `tools/build-entry-obj.sh`, `tools/build-runtime-arm.sh`, `tools/link-x86.sh`, `tools/link-arm.sh`
- **NON modificare `runtime/common/`, `win32_shim.c`, `smack_*`, `tools/dispatch-ir.py`, `tools/build-entry-x86.sh`, `tools/build-libsmacker.sh`, `tools/run-isolated.sh`, e NON lanciare il build completo `build.sh` ora (manca ancora la fine degli altri agenti).**
- NON lanciare altri sottoagenti.

# Missione: Porting B (`host_elf.c` OpenGL/GLES2 palette shader) + D2/D3/Link (`tools/build*.sh`, `tools/link*.sh`)

Leggi `/home/enne2/dev/starcraft-reasm-smacker/HANDOFF-PORTING.md` (sezioni 1, 4.3, 4.4).

1. **Merge accurato di `runtime/shim-port/elf/host_elf.c` (B `8b8734f`):**
   - Estrai `git -C ~/dev/starcraft-reasm-smacker diff b77bf0d..HEAD -- runtime/shim-port/elf/host_elf.c`.
   - Integra `SC_PRESENT=gl` + presenter OpenGL/GLES2 (`g_gl_*`, `gl_present_init`, `present_gl`, `SC_GL_VERIFY`, fallback automatico `gl -> gpu -> cpu`) nel nostro `runtime/shim-port/elf/host_elf.c`.
   - **ATTENZIONE CRITICA — PRESERVA 100% DEI NOSTRI FIX IN `host_elf.c`:**
     - `SC_CONTROL_FILE` (lettura incrementale via `lseek` per `tools/run-isolated.sh`)!
     - Soppressione di `mouse_poll_global()` quando `SC_CONTROL_FILE`/`SC_INPUT_SCRIPT`/`SC_INPUT_REPLAY`/`SC_NO_GLOBAL_MOUSE` è attivo!
   - Copia `tests/x86_gl_palette_test/` da `~/dev/starcraft-reasm-smacker/tests/` ed esegui `./tests/x86_gl_palette_test/check.sh`!
2. **Merge accurato di `tools/build*.sh` e `tools/link*.sh` (`7f3d444`, `76c73b9`, `2be9f8f`, `9d9f4fc`):**
   - Ispeziona `git -C ~/dev/starcraft-reasm-smacker diff b77bf0d..HEAD -- tools/` (escluso `dispatch-ir.py` e `build-libsmacker.sh` che appartengono ad altri agenti!).
   - Integra in `tools/{build.sh,build-runtime-x86.sh,build-shim-x86.sh,build-entry-obj.sh,build-runtime-arm.sh,link-x86.sh,link-arm.sh}`:
     - `storm_scomp` aggiunto alla lista degli oggetti in `build-runtime-x86.sh` e `build-runtime-arm.sh` (mantenendo `storm_crt_unlifted` se già compilato o presente).
     - `-D_GNU_SOURCE=1` nell'AST dump di `gen_shims_bridge.py` dentro `tools/build-shim-x86.sh` (`9d9f4fc`) e flag `-D_GNU_SOURCE=1` per `sched_setaffinity` se necessario!
     - Profili `armv6`/`armv7hf` (`76c73b9`) in `tools/build.sh` / `build-runtime-arm.sh` / `link-arm.sh`.
     - Link `2be9f8f` in `tools/link-x86.sh` (eliminazione padding 208 MiB).
   - **ATTENZIONE CRITICA — PRESERVA TUTTI I NOSTRI FIX IN `tools/`:**
     - **NON perdere** l'override `runtime/common/llasm_cmps.c` in `build-runtime-x86.sh`, `build-shim-x86.sh` e `build-runtime-arm.sh`!
     - **NON perdere** `win32_shell_dlg` in `shim_files` dentro `tools/build-shim-x86.sh` (e `storm_crt_unlifted` se presente in `build-runtime-x86.sh`)!
     - **NON perdere** `rm -f "$OUT"` (`ETXTBSY` safety) in `tools/link-x86.sh`!
     - Verifica `bash -n` su tutti gli script `tools/*.sh` modificati!
3. **Consegna:**
   - Scrivi `reports/REPORT-port-gfx-tools.md` e `reports/STATE-port-gfx-tools.md` (max 70 righe).
