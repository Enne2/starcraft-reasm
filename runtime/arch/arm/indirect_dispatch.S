/* runtime/arm/indirect_dispatch.S — `reasm_indirect`: the one gate for indirect guest branches.
 *
 * The translated code reaches it with `musttail call fastcc void @reasm_indirect(%_cpu* %cpu)`
 * (the emitter's indirect `tcall`, vendor/sr/llasm/llasm.d case "tcall"), so:
 *      r0 = %_cpu*
 * and the requested target is in `_cpu._ind_target` (offset 136), written by the translated code
 * just before the call: the musttail ABI requires the callee signature to equal the caller's, so
 * the target cannot be a second argument. E' un campo di _cpu, quindi PER THREAD.
 *
 * Why this is assembly and not C: the resolved entry must be reached with a *branch*, never a
 * call. Every guest call/ret pair in this port is a branch (`bx`) so that no host frame is added
 * per edge (see cpu_llasm.h and gen_bridge_stubs.py). A `bl` here would leave the frame of this
 * function behind and the guest's own `ret` would unwind into the wrong stack level.
 */
	.text
	.arm
	.syntax unified

	.global	reasm_indirect
	.type	reasm_indirect, %function
reasm_indirect:
	/* Cache hit: no BL/frame; preserve r0 (_cpu) and branch to cached entry. */
	ldr	r3, [r0, #16]
	str	r3, [r0, #140]
	ldr	r1, [r0, #136]
	cmp	r1, #0
	beq	.Lind_slow
	mov	r2, r1, lsr #2
	and	r2, r2, #255
	mov	r3, r1, lsr #10
	and	r3, r3, #15
	orr	r2, r2, r3, lsl #8
	ldr	r12, [r0, #144]
	cmp	r12, #0
	beq	.Lind_slow
	add	r12, r12, r2, lsl #3
	ldr	r3, [r12]
	cmp	r3, r1
	bne	.Lind_slow
	ldr	r3, [r12, #4]
	cmp	r3, #0
	beq	.Lind_slow
	bx	r3
.Lind_slow:
	push	{r4, lr}			@ 8 byte: sp resta allineato a 8 per la chiamata C
	mov	r4, r0				@ r4 = _cpu*
	ldr	r3, [r4, #16]			@ _cpu._esp: ESP emulato *prima* dell'arco
	str	r3, [r4, #140]			@ _cpu._ind_esp (offset 140): solo diagnostica
	ldr	r0, [r4, #136]			@ r0 = _cpu._ind_target (VA guest o indirizzo ARM)
	bl	reasm_indirect_resolve		@ r0 = voce tradotta, oppure 0
	cmp	r0, #0
	beq	.Lunresolved
	mov	r2, r0
	mov	r0, r4				@ ripristina %_cpu per la procedura ospite
	pop	{r4, lr}
	bx	r2				@ salto, non chiamata: nessun frame in più
.Lunresolved:
	mov	r0, r4
	ldr	r1, [r4, #136]			@ il target richiesto
	bl	reasm_indirect_unresolved	@ non ritorna (exit 71)
	b	.					@ irraggiungibile
	.size	reasm_indirect, . - reasm_indirect


/* ---------------------------------------------------------------------------------------------
 * reasm_extern — chiamare un'implementazione C al posto di una procedura guest.
 *
 * Perche' serve, con l'oracolo: una procedura guest viene *chiamata* con `PUSH <ritorno>; tcall`,
 * e la sua `ret` e' `POP tmp1; tcall tmp1` (build/lift-full/run-exe/seg01_code.llinc, righe `;ret`
 * e `;call`). Un'implementazione C non puo' fare ne' l'una ne' l'altra: il suo `ret` ARM
 * tornerebbe al chiamante *host* della dispatch (che l'ha raggiunta con `bx`, non con `bl`)
 * lasciando sullo stack emulato l'indirizzo di ritorno mai consumato. Misurato: l'entry del CRT
 * tornava "senza consumare il sentinella" (work/link/REPORT-flash-crt.md §2.4).
 *
 * Quindi: r0 = %_cpu*, r1 = funzione C. Si chiama la C (frame host di questo trampolino, 16 byte,
 * sp 8-allineato), poi si emula esattamente la `ret` del guest: pop dell'indirizzo di ritorno
 * dallo stack emulato, risoluzione con la stessa tabella/regola della dispatch, salto (`bx`).
 * Nessun frame host resta appeso all'arco guest.
 */
	.global	reasm_extern
	.type	reasm_extern, %function
reasm_extern:
	push	{r4, r5, r6, lr}
	mov	r4, r0				@ %_cpu*
	mov	r5, r1				@ funzione C
	mov	r0, r4
	blx	r5
	mov	r0, r4
	bl	cpu_llasm_pop32			@ r0 = indirizzo di ritorno (host o VA guest)
	mov	r6, r0
	mov	r0, r0
	ldr	r3, [r4, #16]			@ ESP emulato dopo il pop (diagnostica, come in reasm_indirect)
	str	r3, [r4, #140]
	bl	reasm_indirect_resolve
	cmp	r0, #0
	beq	.Lext_unresolved
	mov	r2, r0
	mov	r0, r4				@ %_cpu per il continuamento guest
	pop	{r4, r5, r6, lr}
	bx	r2
.Lext_unresolved:
	mov	r0, r4
	mov	r1, r6
	bl	reasm_indirect_unresolved	@ non ritorna (exit 71)
	b	.
	.size	reasm_extern, . - reasm_extern

/*  ___sse2_available_init (VA guest 0x004F8329) — corpo in runtime/arm/crt_init.c.
 *  Il VA e' nella tabella della dispatch (`--map-extra 0x4F8329=__sse2_available_init`), quindi la
 *  procedura si raggiunge con un salto: questo ingresso la rende indistinguibile da una tradotta.
 */
	.global	__sse2_available_init
	.type	__sse2_available_init, %function
__sse2_available_init:
	ldr	r1, .Lsse2_body
	b	reasm_extern
	.align	2
.Lsse2_body:
	.word	__sse2_available_init_body
	.size	__sse2_available_init, . - __sse2_available_init

/*  ___sse2_available_init di storm.dll (VA guest 0x15040E07) — corpo in crt_init.c.
 *  Voce 0x15059100 della tabella inizializzatori del CRT di storm (0x150590F0..0x15059104).
 */
	.global	__sse2_available_init_storm
	.type	__sse2_available_init_storm, %function
__sse2_available_init_storm:
	ldr	r1, .Lsse2_storm_body
	b	reasm_extern
	.align	2
.Lsse2_storm_body:
	.word	__sse2_available_init_storm_body
	.size	__sse2_available_init_storm, . - __sse2_available_init_storm

/* Storm ordinals 313/334/552: route through the common guest-return trampoline. */
	.global storm_313_arm
	.type storm_313_arm, %function
storm_313_arm:
	ldr r1, .Lstorm313_body
	b reasm_extern
	.align 2
.Lstorm313_body:
	.word storm_313_arm_body
	.size storm_313_arm, . - storm_313_arm

	.global storm_334_arm
	.type storm_334_arm, %function
storm_334_arm:
	ldr r1, .Lstorm334_body
	b reasm_extern
	.align 2
.Lstorm334_body:
	.word storm_334_arm_body
	.size storm_334_arm, . - storm_334_arm

	.global storm_552_arm
	.type storm_552_arm, %function
storm_552_arm:
	ldr r1, .Lstorm552_body
	b reasm_extern
	.align 2
.Lstorm552_body:
	.word storm_552_arm_body
	.size storm_552_arm, . - storm_552_arm
