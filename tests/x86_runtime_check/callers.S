	.text
/* C ABI adapters to the fastcc entry points, including callee-saved register sentinels. */
	.macro CHECK_CALL name
	.globl \name
\name:
	pushl %ebp
	movl %esp,%ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	movl $0x13572468,%ebx
	movl $0x24681357,%esi
	movl $0x35792468,%edi
	movl 12(%ebp),%ecx
	call *8(%ebp)
	cmpl $0x13572468,%ebx
	jne 9f
	cmpl $0x24681357,%esi
	jne 9f
	cmpl $0x35792468,%edi
	jne 9f
	popl %edi
	popl %esi
	popl %ebx
	leave
	ret
9:
	ud2
	.endm
	CHECK_CALL x86_call_fast
	CHECK_CALL x86_call_bridge
	CHECK_CALL x86_call_com

	.globl x86_call_extern
x86_call_extern:
	pushl %ebp
	movl %esp,%ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	movl 8(%ebp),%ecx
	movl 12(%ebp),%eax
	call reasm_extern
	popl %edi
	popl %esi
	popl %ebx
	leave
	ret
