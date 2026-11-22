/* P9 — filmati SMACK nativi (ex task T7).
 *
 * Due agganci consultati dallo shim Win32, con preferenza debole: se questo modulo non è nel
 * link (o SC_SMACK != 1) il comportamento è identico a prima (filmati saltati, nessun falso
 * successo). Vedi smack_elf.c per il contratto misurato su storm.
 */
#ifndef SC_SMACK_ELF_H
#define SC_SMACK_ELF_H

void *win32_smack_load_library(const char *nativename);          /* "smackw32.dll" -> handle o 0 */
void *win32_smack_get_proc(void *module, const char *decorated); /* "_SmackOpen@12" -> funzione o 0 */

#endif
