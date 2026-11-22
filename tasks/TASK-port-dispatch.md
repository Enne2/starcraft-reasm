# Preludio obbligatorio
- Oracolo primario: IDA / `storm.orig.dll` / `StarCraft.orig.exe`, sorgenti in `~/dev/starcraft-reasm-smacker/`.
- **Proprietà esclusiva di questo agente (NON toccare file altrui):**
  - `runtime/common/{cpu_llasm.c,cpu_llasm.h,game_memory_arm.c,game_memory_arm.h,game_main_arm.c,guest_call_arm.c,indirect_dispatch.c,storm_scomp.c,storm_sdraw_arm.c,gen_game_layout.py}`
  - `runtime/arch/x86/indirect_dispatch_x86.S`, `runtime/arch/arm/indirect_dispatch.S`
  - `tools/dispatch-ir.py`
  - `tests/x86_runtime_check/fixture.c`, `tests/x86_shadow_mem_test*`, `tests/x86_scomp_test/`, `tests/x86_perf_bench/`
- **NON modificare `win32_shim.c`, `host_elf.c`, `smack_*`, `llasm_cmps.c`, `tools/build*.sh`, `tools/link*.sh`.**
- NON lanciare altri sottoagenti.

# Missione: Porting A1 + A2 + D1 + C (`#552 SCompDecompress`) + `dispatch-ir.py` + `gen_game_layout.py` + Rebuild `entry-x86`

Leggi `/home/enne2/dev/starcraft-reasm-smacker/HANDOFF-PORTING.md` (sezioni 1 e 4.4).

1. **Diff & Merge preciso (SENZA perdere i nostri fix!):**
   - Per ciascun file di tua proprietà, ispeziona il diff rispetto a `~/dev/starcraft-reasm-smacker/` (e `git -C ~/dev/starcraft-reasm-smacker diff b77bf0d..HEAD -- <file>`):
     - **A1 + D1 (`88893fd`, `9750b22`)**: `cpu_llasm.c/.h` (`_ind_cache` offset 144 per-CPU!), `indirect_dispatch.c`, `indirect_dispatch_x86.S`, `indirect_dispatch.S`, `tests/x86_runtime_check/fixture.c`.
     - **A2 (`daaf929`)**: `game_memory_arm.c/.h` (shadow page table 1 MiB O(1)), e `storm_sdraw_arm.c` -> **ATTENZIONE**: in `storm_sdraw_arm.c` PRESERVA 100% dei nostri fix (`storm_video_skip_enabled` con `proc_range`, `8A 0E/17`, `8B 0E/17`, non-LUT SCode kernels, `SC_SCODE_DUMP`)!
     - **C (`7f3d444`)**: `runtime/common/storm_scomp.c`, `guest_call_arm.c`, `tests/x86_scomp_test/` -> **verifica in `storm_scomp.c` e `dispatch-ir.py` i fix esatti per `#552 SCompDecompress` (`storm_552_arm`)**.
     - **D3 (`76c73b9`)**: `game_main_arm.c` (`SC_AFFINITY` via `thread_affinity.h` — coordinati leggendo se `thread_affinity.h` è già stato copiato o usa una guardia pulita / controlla che esista prima di testare!).
     - **`tools/dispatch-ir.py`**: **ATTENZIONE CRITICA**: il nostro `tools/dispatch-ir.py` ha `RE_TABLE_BASE`, `RE_TEXT_ALIAS` e `RE_PTRTOINT_TEXT` (619 siti `@_text` che risolvono `0x90909090` e `0x0435A008`!). Integra le aggiunte di `smacker` (`_ind_cache` in `%_cpu` + `storm_552_arm` hook) **PRESERVANDO TUTTE LE NOSTRE REGOLE `RE_TABLE_BASE`/`RE_TEXT_ALIAS`/`RE_PTRTOINT_TEXT`**!
     - **`runtime/common/gen_game_layout.py` (`2be9f8f`)**: integra il fix `ALIAS_RE` (`%__[a-z]+\*`) verificando che preservi `--arch x86` e `--arch arm`.
2. **Rigenera gli oggetti IR & Esegui i test di tua proprietà:**
   - Copia `tests/x86_shadow_mem_test*`, `tests/x86_scomp_test/`, `tests/x86_perf_bench/` da `~/dev/starcraft-reasm-smacker/tests/`.
   - Esegui `./tools/build-entry-x86.sh` (per rigenerare `build/lift-full/entry-x86/{StarCraft,storm}.entry.{ll,o}` con `_ind_cache` e `storm_552_arm` + i nostri `@_text` fix!) e verifica con `rg` che `@loc_4FDE60` e gli alias `@_text` siano tutti intatti!
   - Esegui `./tests/x86_shadow_mem_test/check.sh` e `./tests/x86_scomp_test/check.sh` (e verifica compilazione `storm_sdraw_arm.c`, `indirect_dispatch.c`, `cpu_llasm.c`, `storm_scomp.c`, `indirect_dispatch_x86.S` in `/tmp/disp-check/`).
3. **Consegna:**
   - Scrivi `reports/REPORT-port-dispatch.md` e `reports/STATE-port-dispatch.md` (max 70 righe).
