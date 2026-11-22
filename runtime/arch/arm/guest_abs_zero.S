@ runtime/arm/guest_abs_zero.S — simboli che devono valere ZERO nel guest.
@
@ `__fpmath` e' il dispatcher delle trascendenti del CRT MSVC (msvcrt.dll): la CRT tradotta lo
@ chiama solo se il puntatore nella sua tabella e' diverso da zero
@
@     proc loc_404E50                 ; wrapper del CRT
@       mov eax, [loc_51B448]         ; slot della tabella: daddr __fpmath
@       test eax, eax
@       jz  loc_404E60                ; <-- ramo "nessun dispatcher": codice x87 tradotto
@       push dword [esp+4]            ; opcode
@       call eax
@
@ Non esiste una msvcrt.dll da cui prendere il dispatcher, e ricostruirne il protocollo a opcode
@ significherebbe inventare una tabella. Definendo il simbolo come assoluto 0 lo slot vale 0 e la
@ CRT usa il proprio percorso x87 tradotto: e' il ramo previsto per un host senza helper, non uno
@ stub silenzioso (il guest esegue codice vero, tradotto, con la stessa semantica del resto).
	.arm
	.syntax unified
	.global __fpmath
	.set __fpmath, 0
