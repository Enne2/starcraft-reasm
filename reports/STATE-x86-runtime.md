# Stato — runtime x86 i686 (Passo 2)

## Fatto e verificato
- Trampolini i386 in `runtime/x86/`: dispatch indiretta, wrapper C esterno, bridge COM da 256 thunk e `__fpmath=0`.
- `gen_bridge_stubs.py --arch arm|x86`: default ARM; output predefinito e `--arch arm` byte-identici; x86 assembly generato e compilato.
- C condivisi adattati per ucontext i386, `pthread.h`, clock monotono portabile e header layout specifico.
- `gen_pe_image_bytes.py --arch x86` supporta GAS i386 senza alterare il default ARM.
- `tools/build-runtime-x86.sh`: exit 0; 34 oggetti ELF32 Intel 80386, layout/PE byte/ordinali generati.
- `tests/x86_runtime_check/check.sh`: PASS; dispatch, extern, bridge, COM e registri/stacks verificati nativamente.
- Nessun warning nei tre C modificati con `-Wall -Wextra`; nessuna operazione Git o sotto-agente.

## File principali
- `runtime/x86/{indirect_dispatch_x86.S,com_bridge_x86.S,guest_abs_zero_x86.S}`
- `runtime/arm/{gen_bridge_stubs.py,gen_pe_image_bytes.py,cpu_llasm.c,game_main_arm.c,bridge_arm.c}`
- `tools/build-runtime-x86.sh`, `tests/x86_runtime_check/`
- Report: `reports/REPORT-x86-step2.md`

## Aperto
- Non eseguito link del gioco completo né smoke test del gioco; non richiesti dal gate di compilazione runtime di questo passo.
- Test dei bridge limitato ai trampolini e a fixture sintetiche, non a ogni import/API.
