/* runtime/arm/crt_init_arm.c — procedure CRT che il lift non puo' tradurre, con semantica provata.
 *
 * `___sse2_available_init` (StarCraft.exe VA 0x004F8329, IDA: decompilata per intero)
 * ---------------------------------------------------------------------------------
 * Perche' non e' liftata: il corpo esegue `cpuid` (IDA 0x4F835A e 0x4F836D) dopo aver
 * interrogato il bit ID di EFLAGS. Ne' SRW (`vendor/sr/SRW/udis86_dep.c` non ha `cpuid` nella
 * tabella delle flag) ne' `llasm` hanno un caso per `cpuid`: dichiararla in
 * `build/lift-full/external_procedures.sci` e' il meccanismo previsto dalla pipeline per le
 * procedure che il traduttore non copre (stesso trattamento di `__CIpow`, vedi x87_crt_arm.c).
 *
 * Semantica dall'oracolo (IDA 0x4F8329-0x4F83E9, letta per intero):
 *   v0 = EFLAGS; EFLAGS ^= 0x200000 (bit ID); v2 = EFLAGS; EFLAGS = v0
 *   if (v2 != v0) {                       // CPUID disponibile
 *       cpuid(EAX=0) -> vendor in EBX/EDX/ECX (12 byte a Str2)
 *       cpuid(EAX=1) -> v13 = HIBYTE(EDX), v14 = EAX
 *   }
 *   dword_6DC2F0 = 0;  dword_6DC2EC = 0;  // 0x4F8376 / 0x4F837D: si azzerano SEMPRE
 *   if ((v13 & 4) && has_osfxsr_set(...)) // (v13 & 4) = bit 26 di EDX = SSE2
 *   { dword_6DC2F0 = 1; if (vendor!="AuthenticAMD" || family!=15) dword_6DC2EC = 1; }
 *   return 0;
 *
 * Sul target ARM1176 non esiste CPUID e non esiste SSE2: il test del bit ID deve fallire, quindi
 * il percorso originale lascia i due flag a 0 e ritorna 0. Non e' un "valore comodo": e' il
 * risultato del ramo che l'originale prende su una CPU senza CPUID, ed e' anche l'unico corretto
 * per il port, perche' il CRT MSVC usa __sse2_available (0x006DC2F0) per scegliere le varianti
 * SSE2 di memcpy/memset del *proprio* codice guest, che il lift non sa tradurre (movaps/movdqa).
 * Con il flag a 0 quelle varianti non vengono mai selezionate.
 *
 * I due globali stanno in .data dell'immagine (0x0050C000-0x006DD694), che il port mappa ai VA
 * originali (guest_data.ld): si scrive direttamente al VA, come fa il codice tradotto.
 *
 * ATTENZIONE al nome: qui c'e' solo il *corpo*. L'ingresso che la tabella della dispatch chiama e'
 * `__sse2_available_init`, scritto in runtime/arm/indirect_dispatch.S, che dopo il ritorno di
 * questa funzione emula la `ret` del guest (pop dell'indirizzo di ritorno + salto): un `ret` C
 * lascerebbe l'indirizzo di ritorno sullo stack emulato (misurato, report §2.4).
 */
#include "cpu_llasm.h"

#include <stdint.h>

#define SSE2_AVAILABLE      0x006DC2F0u   /* IDA dword_6DC2F0 */
#define SSE2_AVAILABLE_AMD  0x006DC2ECu   /* IDA dword_6DC2EC */

void __sse2_available_init_body(_cpu *cpu);
void __sse2_available_init_body(_cpu *cpu) {
    *(volatile uint32_t *) (uintptr_t) SSE2_AVAILABLE = 0u;
    *(volatile uint32_t *) (uintptr_t) SSE2_AVAILABLE_AMD = 0u;
    cpu->_eax = 0u;                        /* return 0 (0x4F83E9) */
}

/* `___sse2_available_init` di storm.dll (VA 0x15040E07, IDA: decompilata per intero)
 * ------------------------------------------------------------------------------------
 * Stessa procedura della gemella dell'eseguibile, ma nella DLL, e con i suoi due globali:
 * `dword_1505FC20` (0x15040E54) e `dword_1505FC1C` (0x15040E5B), azzerati SEMPRE prima del test.
 *   v0 = EFLAGS; EFLAGS ^= 0x200000 (bit ID); v2 = EFLAGS; EFLAGS = v0
 *   if (v2 != v0) { cpuid(0) -> vendor; cpuid(1) -> v13 = HIBYTE(EDX), v14 = EAX }
 *   dword_1505FC20 = 0; dword_1505FC1C = 0;
 *   if ((v13 & 4) && sub_15040DD2(...))                     // bit 26 di EDX = SSE2
 *   { dword_1505FC20 = 1; if (vendor!="AuthenticAMD" || family != 15) dword_1505FC1C = 1; }
 *   return 0;
 * Sul target ARM1176 il bit ID non è commutabile (nessuna CPUID): il ramo `v2 != v0` non viene
 * preso, v13 resta 0, i due flag restano 0 e la procedura ritorna 0. È il ramo che l'originale
 * prende su una CPU senza CPUID, non un valore scelto per comodità.
 *
 * PERCHÉ SERVE: la tabella degli inizializzatori del CRT di storm (0x150590F0..0x15059104,
 * camminata da `sub_1500810F` = `_initterm` del CRT, chiamata dal DllMain) ha questa voce a
 * 0x15059100: senza l'ingresso nella tabella della dispatch la chiamata finiva sullo stub
 * `crt_unlifted_15040E07` (stop osservato: "API NON IMPLEMENTATA",
 * build/qa/run-e0a11cf2.log). Non si può liftare: `cpuid` non è tradotto da SRW/llasm.
 */
#define SSE2_AVAILABLE_STORM      0x1505FC20u   /* IDA dword_1505FC20 */
#define SSE2_AVAILABLE_STORM_AMD  0x1505FC1Cu   /* IDA dword_1505FC1C */

void __sse2_available_init_storm_body(_cpu *cpu);
void __sse2_available_init_storm_body(_cpu *cpu) {
    *(volatile uint32_t *) (uintptr_t) SSE2_AVAILABLE_STORM = 0u;
    *(volatile uint32_t *) (uintptr_t) SSE2_AVAILABLE_STORM_AMD = 0u;
    cpu->_eax = 0u;                        /* return 0 (0x15040EC7) */
}
