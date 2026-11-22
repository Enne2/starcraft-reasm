/* runtime/arm/tls_arm.S — `__aeabi_read_tp` per le variabili `__thread` del port.
 *
 * Perche' serve: `cpu_llasm_current()` e gli `jmp_buf` della chiamata guest sono diventati
 * `__thread` (un thread guest ha il suo `_cpu`, vedi work/link/REPORT-flash-heap.md §5). Il
 * compilatore ARM EABI emette una chiamata a `__aeabi_read_tp` per ricavare il thread pointer,
 * ma la glibc del sysroot (>= 2.32) non esporta piu' l'helper: lo deve fornire il runtime.
 *
 * Implementazione canonica (glibc sysdeps/arm/aeabi_read_tp.S): leggi il registro TLS del core.
 * ARM1176 e' ARMv6K, quindi TPIDRURO (cp15 c13 c0 3) esiste; qemu-user lo emula.
 */
	.text
	.arm
	.syntax unified

	.global	__aeabi_read_tp
	.type	__aeabi_read_tp, %function
__aeabi_read_tp:
	mrc	p15, 0, r0, c13, c0, 3		@ TPIDRURO: thread pointer dello spazio utente
	bx	lr
	.size	__aeabi_read_tp, . - __aeabi_read_tp
