/* i386 tail-dispatch gates: fastcc carries _cpu in ECX; cdecl helpers use stack args. */
	.text
	.globl x86_call_guest_fast
	.type x86_call_guest_fast,@function
/* cdecl(cpu, entry): transfer CPU to fastcc ECX and tail-enter with caller's return address. */
x86_call_guest_fast:
	movl 4(%esp), %ecx
	movl 8(%esp), %eax
	jmp *%eax
	.size x86_call_guest_fast,.-x86_call_guest_fast

	.globl reasm_indirect
	.type reasm_indirect,@function
reasm_indirect:
	/* Cache hit: ECX remains the fastcc _cpu argument, so tail-jump with no frame. */
	movl 16(%ecx), %edx
	movl %edx, 140(%ecx)
	movl 136(%ecx), %eax
	testl %eax, %eax
	jz .Lind_slow
	movl %eax, %edx
	shrl $2, %edx
	andl $4095, %edx
	pushl %esi
	movl 144(%ecx), %esi
	testl %esi, %esi
	jz .Lind_cache_miss
	shll $3, %edx
	cmpl (%esi,%edx), %eax
	jne .Lind_cache_miss
	movl 4(%esi,%edx), %edx
	testl %edx, %edx
	jz .Lind_cache_miss
	popl %esi
	jmp *%edx
.Lind_cache_miss:
	popl %esi
	jmp .Lind_slow
.Lind_slow:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %ecx
	pushl %esi
	movl %ecx, %ebx              /* cpu */
	movl 16(%ebx), %edx
	movl %edx, 140(%ebx)
	andl $-16, %esp
	subl $12, %esp
	pushl 136(%ebx)
	call reasm_indirect_resolve
	addl $16, %esp
	testl %eax, %eax
	jz .Lind_unresolved
	movl %eax, %edx
	movl -8(%ebp), %ecx
	movl -4(%ebp), %ebx
	movl %ebp, %esp
	popl %ebp
	jmp *%edx
.Lind_unresolved:
	andl $-16, %esp
	subl $8, %esp
	pushl 136(%ebx)
	pushl %ebx
	call reasm_indirect_unresolved
	ud2
	.size reasm_indirect,.-reasm_indirect

/* Entry ABI: ECX=cpu, EAX=cdecl C target. */
	.globl reasm_extern
	.type reasm_extern,@function
reasm_extern:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %ecx
	pushl %eax
	movl %ecx, %ebx              /* cpu */
	movl %eax, %esi              /* C target */
	andl $-16, %esp
	subl $12, %esp
	pushl %ebx
	call *%esi
	addl $16, %esp
	andl $-16, %esp
	subl $12, %esp
	pushl %ebx
	call cpu_llasm_pop32
	addl $16, %esp
	movl %eax, %esi                 /* guest return VA */
	movl 16(%ebx), %edx
	movl %edx, 140(%ebx)
	andl $-16, %esp
	subl $12, %esp
	pushl %esi
	call reasm_indirect_resolve
	addl $16, %esp
	testl %eax, %eax
	jz .Lext_unresolved
	movl %eax, %edx
	movl -12(%ebp), %ecx
	movl -8(%ebp), %esi
	movl -4(%ebp), %ebx
	movl %ebp, %esp
	popl %ebp
	jmp *%edx
.Lext_unresolved:
	andl $-16, %esp
	subl $8, %esp
	pushl %esi
	pushl %ebx
	call reasm_indirect_unresolved
	ud2
	.size reasm_extern,.-reasm_extern

	.macro EXTERN_STUB name, body
	.globl \name
	.type \name,@function
\name:
	movl $\body, %eax
	jmp reasm_extern
	.size \name,.-\name
	.endm
	.extern storm_313_arm_body
	.extern storm_334_arm_body
	.extern storm_552_arm_body
	.extern __sse2_available_init_body
	.extern __sse2_available_init_storm_body
	EXTERN_STUB storm_313_arm, storm_313_arm_body
	EXTERN_STUB storm_334_arm, storm_334_arm_body
	EXTERN_STUB storm_552_arm, storm_552_arm_body
	EXTERN_STUB __sse2_available_init, __sse2_available_init_body
	EXTERN_STUB __sse2_available_init_storm, __sse2_available_init_storm_body
